#include "apngasm.h"
#include <cstdlib>
#include <png.h>
#include <zlib.h>
#ifdef APNG_SPECS_SUPPORTED
#include "spec/specreader.h"
#include "spec/specwriter.h"
#endif
#include "listener/apngasmlistener.h"

#if defined(_MSC_VER) && _MSC_VER >= 1300
#define swap16(data) _byteswap_ushort(data)
#define swap32(data) _byteswap_ulong(data)
#elif defined(__linux__)
#include <byteswap.h>
#define swap16(data) bswap_16(data)
#define swap32(data) bswap_32(data)
#elif defined(__FreeBSD__)
#include <sys/endian.h>
#define swap16(data) bswap16(data)
#define swap32(data) bswap32(data)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define swap16(data) OSSwapInt16(data)
#define swap32(data) OSSwapInt32(data)
#else
namespace {
  unsigned short swap16(unsigned short data) {return((data & 0xFF) << 8) | ((data >> 8) & 0xFF);}
  unsigned int swap32(unsigned int data) {return((data & 0xFF) << 24) | ((data & 0xFF00) << 8) | ((data >> 8) & 0xFF00) | ((data >> 24) & 0xFF);}
}
#endif

#define notabc(c) ((c) < 65 || (c) > 122 || ((c) > 90 && (c) < 97))

#define id_IHDR 0x52444849
#define id_acTL 0x4C546361
#define id_fcTL 0x4C546366
#define id_IDAT 0x54414449
#define id_fdAT 0x54416466
#define id_IEND 0x444E4549

namespace {

  typedef struct { unsigned int num; unsigned char r, g, b, a; } COLORS;

  apngasm::listener::APNGAsmListener defaultListener;

  int compareColors(const void *arg1, const void *arg2)
  {
    if ( ((COLORS*)arg1)->a != ((COLORS*)arg2)->a )
      return (int)(((COLORS*)arg1)->a) - (int)(((COLORS*)arg2)->a);

    if ( ((COLORS*)arg1)->num != ((COLORS*)arg2)->num )
      return (int)(((COLORS*)arg2)->num) - (int)(((COLORS*)arg1)->num);

    if ( ((COLORS*)arg1)->r != ((COLORS*)arg2)->r )
      return (int)(((COLORS*)arg1)->r) - (int)(((COLORS*)arg2)->r);

    if ( ((COLORS*)arg1)->g != ((COLORS*)arg2)->g )
      return (int)(((COLORS*)arg1)->g) - (int)(((COLORS*)arg2)->g);

    return (int)(((COLORS*)arg1)->b) - (int)(((COLORS*)arg2)->b);
  }

} // unnamed namespace

namespace apngasm {

  //Construct APNGAsm object
  APNGAsm::APNGAsm(void)
    : _pListener(&defaultListener)
  {
    // nop
  }

  //Construct APNGAsm object
  APNGAsm::APNGAsm(const std::vector<APNGFrame> &frames)
    : _pListener(&defaultListener)
  {
    _frames.insert(_frames.end(), frames.begin(), frames.end());
  }

  //Returns the version of APNGAsm
  const char* APNGAsm::version(void) const
  {
    return APNGASM_VERSION;
  }

  // Returns the vector of frames.
  const std::vector<APNGFrame>& APNGAsm::getFrames() const
  {
    return _frames;
  }

  size_t APNGAsm::frameCount()
  {
    return _frames.size();
  }

  size_t APNGAsm::reset()
  {
    if (_frames.empty())
      return 0;

    for (size_t n = 0; n < _frames.size(); ++n)
    {
      delete[] _frames[n]._pixels;
      delete[] _frames[n]._rows;
    }
    _frames.clear();

    return _frames.size();
  }

  //Adds a frame from a file
  //Returns the frame number in the frame vector
  //Uses default delay of 10ms if not specified
  size_t APNGAsm::addFrame(const std::string &filePath, unsigned int delayNum, unsigned int delayDen)
  {
    if( _pListener->onPreAddFrame(filePath, delayNum, delayDen) )
    {
      APNGFrame frame = APNGFrame(filePath, delayNum, delayDen);
      _frames.push_back(frame);
      _pListener->onPostAddFrame(filePath, delayNum, delayDen);
    }
    return _frames.size();
  }

  //Adds an APNGFrame object to the frame vector
  //Returns the frame number in the frame vector
  size_t APNGAsm::addFrame(const APNGFrame &frame)
  {
    if( _pListener->onPreAddFrame(frame) )
    {
      _frames.push_back(frame);
      _pListener->onPostAddFrame(frame);
    }
    return _frames.size();
  }

  //Adds an APNGFrame object to the frame vector
  //Returns a frame vector with the added frames
  APNGAsm& APNGAsm::operator << (const APNGFrame &frame)
  {
    addFrame(frame);
    return *this;
  }

#ifdef APNG_SPECS_SUPPORTED
  //Loads an animation spec from JSON or XML
  //Returns a frame vector with the loaded frames
  //Loaded frames are added to the end of the frame vector
  const std::vector<APNGFrame>& APNGAsm::loadAnimationSpec(const std::string &filePath)
  {
    spec::SpecReader reader(this);

    if( !reader.read(filePath) )
    {
      // read failed.
    }

    return _frames;
  }

  // Save json file.
  bool APNGAsm::saveJson(const std::string& outputPath, const std::string& imageDir) const
  {
    bool result = false;
    if( _pListener->onPreSave(outputPath) )
    {
      spec::SpecWriter writer(this, _pListener);
      if( (result = writer.writeJson(outputPath, imageDir)) )
        _pListener->onPostSave(outputPath);
    }
    return result;
  }

  // Save xml file.
  bool APNGAsm::saveXml(const std::string& outputPath, const std::string& imageDir) const
  {
    bool result = false;
    if( _pListener->onPreSave(outputPath) )
    {
      spec::SpecWriter writer(this, _pListener);
      if( (result = writer.writeXml(outputPath, imageDir)) )
        _pListener->onPostSave(outputPath);
    }
    return result;
  }
#endif

  // Set APNGAsmListener.
  // If argument is NULL, set default APNGAsmListener.
  void APNGAsm::setAPNGAsmListener(listener::IAPNGAsmListener* pListener)
  {
    _pListener = (pListener==NULL) ? &defaultListener : pListener;
  }

#ifdef APNG_WRITE_SUPPORTED
  //Assembles and outputs an APNG file
  //Returns the assembled file object
  //If no output path is specified only the file object is returned
  bool APNGAsm::assemble(const std::string &outputPath)
  {
    if (_frames.empty())
      return false;

    if( !_pListener->onPreSave(outputPath) )
      return false;

    _width  = _frames[0]._width;
    _height = _frames[0]._height;
    _size   = _width * _height;

    unsigned char coltype = findCommonType();

    if (upconvertToCommonType(coltype))
      return false;

    dirtyTransparencyOptimization(coltype);

    coltype = downconvertOptimizations(coltype, false, false);

    if( !save(outputPath, coltype, 0, 0) )
      return false;

    _pListener->onPostSave(outputPath);
    return true;
  }

  unsigned char APNGAsm::findCommonType(void)
  {
    unsigned char coltype = _frames[0]._colorType;

    for (size_t n = 1; n < _frames.size(); ++n)
    {
      if (_frames[0]._paletteSize != _frames[n]._paletteSize || memcmp(_frames[0]._palette, _frames[n]._palette, _frames[0]._paletteSize*3) != 0)
        coltype = 6;
      else
      if (_frames[0]._transparencySize != _frames[n]._transparencySize || memcmp(_frames[0]._transparency, _frames[n]._transparency, _frames[0]._transparencySize) != 0)
        coltype = 6;
      else
      if (_frames[n]._colorType != 3)
      {
        if (coltype != 3)
          coltype |= _frames[n]._colorType;
        else
          coltype = 6;
      }
      else
        if (coltype != 3)
          coltype = 6;
    }
    return coltype;
  }

  int APNGAsm::upconvertToCommonType(unsigned char coltype)
  {
    unsigned char  * sp;
    unsigned char  * dp;
    unsigned char    r, g, b;
    unsigned int     j;

    for (size_t n = 0; n < _frames.size(); ++n)
    {
      if (coltype == 6 && _frames[n]._colorType != 6)
      {
        unsigned char * dst = new unsigned char[_size * 4];

        if (_frames[n]._colorType == 0)
        {
          sp = _frames[n]._pixels;
          dp = dst;
          if (_frames[n]._transparencySize == 0)
          for (j=0; j<_size; j++)
          {
            *dp++ = *sp;
            *dp++ = *sp;
            *dp++ = *sp++;
            *dp++ = 255;
          }
          else
          for (j=0; j<_size; j++)
          {
            g = *sp++;
            *dp++ = g;
            *dp++ = g;
            *dp++ = g;
            *dp++ = (_frames[n]._transparency[1] == g) ? 0 : 255;
          }
        }
        else
        if (_frames[n]._colorType == 2)
        {
          sp = _frames[n]._pixels;
          dp = dst;
          if (_frames[n]._transparencySize == 0)
          for (j=0; j<_size; j++)
          {
            *dp++ = *sp++;
            *dp++ = *sp++;
            *dp++ = *sp++;
            *dp++ = 255;
          }
          else
          for (j=0; j<_size; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            *dp++ = r;
            *dp++ = g;
            *dp++ = b;
            *dp++ = (_frames[n]._transparency[1] == r && _frames[n]._transparency[3] == g && _frames[n]._transparency[5] == b) ? 0 : 255;
          }
        }
        else
        if (_frames[n]._colorType == 3)
        {
          sp = _frames[n]._pixels;
          dp = dst;
          for (j=0; j<_size; j++, sp++)
          {
            *dp++ = _frames[n]._palette[*sp].r;
            *dp++ = _frames[n]._palette[*sp].g;
            *dp++ = _frames[n]._palette[*sp].b;
            *dp++ = _frames[n]._transparency[*sp];
          }
        }
        else
        if (_frames[n]._colorType == 4)
        {
          sp = _frames[n]._pixels;
          dp = dst;
          for (j=0; j<_size; j++)
          {
            *dp++ = *sp;
            *dp++ = *sp;
            *dp++ = *sp++;
            *dp++ = *sp++;
          }
        }
        delete[] _frames[n]._pixels;
        _frames[n]._pixels = dst;
        _frames[n]._colorType = coltype;

        for (j=0; j<_frames[n]._height; ++j)
          _frames[n]._rows[j] = dst + j * _frames[n]._width * 4;
      }
      else
      if (coltype == 4 && _frames[n]._colorType == 0)
      {
        unsigned char * dst = new unsigned char[_size * 2];

        sp = _frames[n]._pixels;
        dp = dst;
        for (j=0; j<_size; j++)
        {
          *dp++ = *sp++;
          *dp++ = 255;
        }
        delete[] _frames[n]._pixels;
        _frames[n]._pixels = dst;
        _frames[n]._colorType = coltype;

        for (j=0; j<_frames[n]._height; ++j)
          _frames[n]._rows[j] = dst + j * _frames[n]._width * 2;
      }
      else
      if (coltype == 2 && _frames[n]._colorType == 0)
      {
        unsigned char * dst = new unsigned char[_size * 3];

        sp = _frames[n]._pixels;
        dp = dst;
        for (j=0; j<_size; j++)
        {
          *dp++ = *sp;
          *dp++ = *sp;
          *dp++ = *sp++;
        }
        delete[] _frames[n]._pixels;
        _frames[n]._pixels = dst;
        _frames[n]._colorType = coltype;

        for (j=0; j<_frames[n]._height; ++j)
          _frames[n]._rows[j] = dst + j * _frames[n]._width * 3;
      }
    }
    return 0;
  }

  void APNGAsm::dirtyTransparencyOptimization(unsigned char coltype)
  {
    if (coltype == 6)
    {
      for (size_t n = 0; n < _frames.size(); ++n)
      {
        unsigned char * sp = _frames[n]._pixels;
        for (unsigned int j=0; j<_size; j++, sp+=4)
          if (sp[3] == 0)
             sp[0] = sp[1] = sp[2] = 0;
      }
    }
    else
    if (coltype == 4)
    {
      for (size_t n = 0; n < _frames.size(); ++n)
      {
        unsigned char * sp = _frames[n]._pixels;
        for (unsigned int j=0; j<_size; j++, sp+=2)
          if (sp[1] == 0)
            sp[0] = 0;
      }
    }
  }

  unsigned char APNGAsm::downconvertOptimizations(unsigned char coltype, bool keep_palette, bool keep_coltype)
  {
    unsigned int     has_tcolor = 0;
    unsigned int     colors = 0;
    unsigned char  * sp;
    unsigned char  * dp;
    unsigned char    r, g, b, a;
    unsigned int     i, j, k;
    unsigned char    cube[4096];
    unsigned char    gray[256];
    COLORS           col[256];

    memset(&cube, 0, sizeof(cube));
    memset(&gray, 0, sizeof(gray));

    for (i=0; i<256; i++)
    {
      col[i].num = 0;
      col[i].r = col[i].g = col[i].b = i;
      col[i].a = _trns[i] = 255;
    }
    _palsize = 0;
    _trnssize = 0;

    if (coltype == 6 && !keep_coltype)
    {
      int transparent = 255;
      int simple_trans = 1;
      int grayscale = 1;

      for (size_t n = 0; n < _frames.size(); ++n)
      {
        sp = _frames[n]._pixels;
        for (j=0; j<_size; j++)
        {
          r = *sp++;
          g = *sp++;
          b = *sp++;
          a = *sp++;
          transparent &= a;

          if (a != 0)
          {
            if (a != 255)
              simple_trans = 0;
            else
              if (((r | g | b) & 15) == 0)
                cube[(r<<4) + g + (b>>4)] = 1;

            if (r != g || g != b)
              grayscale = 0;
            else
              gray[r] = 1;
          }

          if (colors <= 256)
          {
            int found = 0;
            for (k=0; k<colors; k++)
            if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
            {
              found = 1;
              col[k].num++;
              break;
            }
            if (found == 0)
            {
              if (colors < 256)
              {
                col[colors].num++;
                col[colors].r = r;
                col[colors].g = g;
                col[colors].b = b;
                col[colors].a = a;
                if (a == 0) has_tcolor = 1;
              }
              colors++;
            }
          }
        }
      }

      if (grayscale && simple_trans && colors<=256) /* 6 -> 0 */
      {
        coltype = 0;

        for (i=0; i<256; i++)
        if (gray[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = i;
          _trnssize = 2;
          break;
        }

        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = dp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            if (*sp++ == 0)
              *dp++ = _trns[1];
            else
              *dp++ = g;
          }
        }
      }
      else
      if (colors<=256)   /* 6 -> 3 */
      {
        coltype = 3;

        if (has_tcolor==0 && colors<256)
          col[colors++].a = 0;

        qsort(&col[0], colors, sizeof(COLORS), compareColors);

        _palsize = colors;
        for (i=0; i<colors; i++)
        {
          _palette[i].r = col[i].r;
          _palette[i].g = col[i].g;
          _palette[i].b = col[i].b;
          _trns[i]      = col[i].a;
          if (_trns[i] != 255) _trnssize = i+1;
        }

        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = dp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;
            a = *sp++;
            for (k=0; k<colors; k++)
              if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
                break;
            *dp++ = k;
          }
        }
      }
      else
      if (grayscale)     /* 6 -> 4 */
      {
        coltype = 4;
        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = dp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            r = *sp++;
            g = *sp++;
            *dp++ = *sp++;
            *dp++ = *sp++;
          }
        }
      }
      else
      if (simple_trans)  /* 6 -> 2 */
      {
        for (i=0; i<4096; i++)
        if (cube[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = (i>>4)&0xF0;
          _trns[2] = 0;
          _trns[3] = i&0xF0;
          _trns[4] = 0;
          _trns[5] = (i<<4)&0xF0;
          _trnssize = 6;
          break;
        }
        if (transparent == 255)
        {
          coltype = 2;
          for (size_t n = 0; n < _frames.size(); ++n)
          {
            sp = dp = _frames[n]._pixels;
            for (j=0; j<_size; j++)
            {
              *dp++ = *sp++;
              *dp++ = *sp++;
              *dp++ = *sp++;
              sp++;
            }
          }
        }
        else
        if (_trnssize != 0)
        {
          coltype = 2;
          for (size_t n = 0; n < _frames.size(); ++n)
          {
            sp = dp = _frames[n]._pixels;
            for (j=0; j<_size; j++)
            {
              r = *sp++;
              g = *sp++;
              b = *sp++;
              a = *sp++;
              if (a == 0)
              {
                *dp++ = _trns[1];
                *dp++ = _trns[3];
                *dp++ = _trns[5];
              }
              else
              {
                *dp++ = r;
                *dp++ = g;
                *dp++ = b;
              }
            }
          }
        }
      }
    }
    else
    if (coltype == 2)
    {
      int grayscale = 1;

      for (size_t n = 0; n < _frames.size(); ++n)
      {
        sp = _frames[n]._pixels;
        for (j=0; j<_size; j++)
        {
          r = *sp++;
          g = *sp++;
          b = *sp++;

          if (_frames[n]._transparencySize == 0)
            if (((r | g | b) & 15) == 0)
              cube[(r<<4) + g + (b>>4)] = 1;

          if (r != g || g != b)
            grayscale = 0;
          else
            gray[r] = 1;

          if (colors <= 256)
          {
            int found = 0;
            for (k=0; k<colors; k++)
            if (col[k].r == r && col[k].g == g && col[k].b == b)
            {
              found = 1;
              col[k].num++;
              break;
            }
            if (found == 0)
            {
              if (colors < 256)
              {
                col[colors].num++;
                col[colors].r = r;
                col[colors].g = g;
                col[colors].b = b;
                if (_frames[n]._transparencySize == 6 && _frames[n]._transparency[1] == r && _frames[n]._transparency[3] == g && _frames[n]._transparency[5] == b)
                {
                  col[colors].a = 0;
                  has_tcolor = 1;
                }
              }
              colors++;
            }
          }
        }
      }

      if (grayscale && colors<=256 && !keep_coltype) /* 2 -> 0 */
      {
        for (i=0; i<256; i++)
        if (gray[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = i;
          _trnssize = 2;
          break;
        }
        if (_frames[0]._transparencySize == 0)
        {
          coltype = 0;
          for (size_t n = 0; n < _frames.size(); ++n)
          {
            sp = dp = _frames[n]._pixels;
            for (j=0; j<_size; j++, sp+=3)
              *dp++ = *sp;
          }
        }
        else
        if (_trnssize != 0)
        {
          coltype = 0;
          for (size_t n = 0; n < _frames.size(); ++n)
          {
            sp = dp = _frames[n]._pixels;
            for (j=0; j<_size; j++)
            {
              r = *sp++;
              g = *sp++;
              b = *sp++;
              if (_frames[n]._transparency[1] == r && _frames[n]._transparency[3] == g && _frames[n]._transparency[5] == b)
                *dp++ = _trns[1];
              else
                *dp++ = g;
            }
          }
        }
      }
      else
      if (colors<=256 && !keep_coltype)   /* 2 -> 3 */
      {
        coltype = 3;

        if (has_tcolor==0 && colors<256)
          col[colors++].a = 0;

        qsort(&col[0], colors, sizeof(COLORS), compareColors);

        _palsize = colors;
        for (i=0; i<colors; i++)
        {
          _palette[i].r = col[i].r;
          _palette[i].g = col[i].g;
          _palette[i].b = col[i].b;
          _trns[i]      = col[i].a;
          if (_trns[i] != 255) _trnssize = i+1;
        }

        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = dp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            r = *sp++;
            g = *sp++;
            b = *sp++;

            for (k=0; k<colors; k++)
              if (col[k].r == r && col[k].g == g && col[k].b == b)
                break;
            *dp++ = k;
          }
        }
      }
      else /* 2 -> 2 */
      {
        if (_frames[0]._transparencySize != 0)
        {
          memcpy(_trns, _frames[0]._transparency, _frames[0]._transparencySize);
          _trnssize = _frames[0]._transparencySize;
        }
        else
        for (i=0; i<4096; i++)
        if (cube[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = (i>>4)&0xF0;
          _trns[2] = 0;
          _trns[3] = i&0xF0;
          _trns[4] = 0;
          _trns[5] = (i<<4)&0xF0;
          _trnssize = 6;
          break;
        }
      }
    }
    else
    if (coltype == 4 && !keep_coltype)
    {
      int simple_trans = 1;

      for (size_t n = 0; n < _frames.size(); ++n)
      {
        sp = _frames[n]._pixels;
        for (j=0; j<_size; j++)
        {
          g = *sp++;
          a = *sp++;

          if (a != 0)
          {
            if (a != 255)
              simple_trans = 0;
            else
              gray[g] = 1;
          }

          if (colors <= 256)
          {
            int found = 0;
            for (k=0; k<colors; k++)
            if (col[k].g == g && col[k].a == a)
            {
              found = 1;
              col[k].num++;
              break;
            }
            if (found == 0)
            {
              if (colors < 256)
              {
                col[colors].num++;
                col[colors].r = g;
                col[colors].g = g;
                col[colors].b = g;
                col[colors].a = a;
                if (a == 0) has_tcolor = 1;
              }
              colors++;
            }
          }
        }
      }

      if (simple_trans && colors<=256)   /* 4 -> 0 */
      {
        coltype = 0;

        for (i=0; i<256; i++)
        if (gray[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = i;
          _trnssize = 2;
          break;
        }

        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = dp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            g = *sp++;
            if (*sp++ == 0)
              *dp++ = _trns[1];
            else
              *dp++ = g;
          }
        }
      }
      else
      if (colors<=256)   /* 4 -> 3 */
      {
        coltype = 3;

        if (has_tcolor==0 && colors<256)
          col[colors++].a = 0;

        qsort(&col[0], colors, sizeof(COLORS), compareColors);

        _palsize = colors;
        for (i=0; i<colors; i++)
        {
          _palette[i].r = col[i].r;
          _palette[i].g = col[i].g;
          _palette[i].b = col[i].b;
          _trns[i]      = col[i].a;
          if (_trns[i] != 255) _trnssize = i+1;
        }

        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = dp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            g = *sp++;
            a = *sp++;
            for (k=0; k<colors; k++)
              if (col[k].g == g && col[k].a == a)
                break;
            *dp++ = k;
          }
        }
      }
    }
    else
    if (coltype == 3)
    {
      int simple_trans = 1;
      int grayscale = 1;

      for (int c=0; c<_frames[0]._paletteSize; c++)
      {
        col[c].r = _frames[0]._palette[c].r;
        col[c].g = _frames[0]._palette[c].g;
        col[c].b = _frames[0]._palette[c].b;
        col[c].a = _frames[0]._transparency[c];
      }

      for (size_t n = 0; n < _frames.size(); ++n)
      {
        sp = _frames[n]._pixels;
        for (j=0; j<_size; j++)
          col[*sp++].num++;
      }

      for (i=0; i<256; i++)
      if (col[i].num != 0)
      {
        colors = i+1;
        if (col[i].a != 0)
        {
          if (col[i].a != 255)
            simple_trans = 0;
          else
          if (col[i].r != col[i].g || col[i].g != col[i].b)
            grayscale = 0;
          else
            gray[col[i].g] = 1;
        }
        else
          has_tcolor = 1;
      }

      if (grayscale && simple_trans && !keep_coltype) /* 3 -> 0 */
      {
        for (i=0; i<256; i++)
        if (gray[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = i;
          _trnssize = 2;
          break;
        }
        if (!has_tcolor)
        {
          coltype = 0;
          for (size_t n = 0; n < _frames.size(); ++n)
          {
            sp = _frames[n]._pixels;
            for (j=0; j<_size; j++, sp++)
              *sp = _frames[n]._palette[*sp].g;
          }
        }
        else
        if (_trnssize != 0)
        {
          coltype = 0;
          for (size_t n = 0; n < _frames.size(); ++n)
          {
            sp = _frames[n]._pixels;
            for (j=0; j<_size; j++, sp++)
            {
              if (col[*sp].a == 0)
                *sp = _trns[1];
              else
                *sp = _frames[n]._palette[*sp].g;
            }
          }
        }
      }
      else
      if (!keep_palette)                 /* 3 -> 3 */
      {
        for (i=0; i<256; i++)
        if (col[i].num == 0)
        {
          col[i].a = 255;
          if (!has_tcolor)
          {
            col[i].a = 0;
            has_tcolor = 1;
          }
        }

        qsort(&col[0], 256, sizeof(COLORS), compareColors);

        for (i=0; i<256; i++)
        {
          _palette[i].r = col[i].r;
          _palette[i].g = col[i].g;
          _palette[i].b = col[i].b;
          _trns[i]      = col[i].a;
          if (col[i].num != 0)
            _palsize = i+1;
          if (_trns[i] != 255)
            _trnssize = i+1;
        }

        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
          {
            r = _frames[n]._palette[*sp].r;
            g = _frames[n]._palette[*sp].g;
            b = _frames[n]._palette[*sp].b;
            a = _frames[n]._transparency[*sp];

            for (k=0; k<_palsize; k++)
              if (col[k].r == r && col[k].g == g && col[k].b == b && col[k].a == a)
                break;
            *sp++ = k;
          }
        }
      }
      else
      {
        _palsize = _frames[0]._paletteSize;
        _trnssize = _frames[0]._transparencySize;
        for (i=0; i<_palsize; i++)
        {
          _palette[i].r = col[i].r;
          _palette[i].g = col[i].g;
          _palette[i].b = col[i].b;
        }
        for (i=0; i<_trnssize; i++)
          _trns[i] = col[i].a;
      }
    }
    else
    if (coltype == 0)  /* 0 -> 0 */
    {
      if (_frames[0]._transparencySize != 0)
      {
        memcpy(_trns, _frames[0]._transparency, _frames[0]._transparencySize);
        _trnssize = _frames[0]._transparencySize;
      }
      else
      {
        for (size_t n = 0; n < _frames.size(); ++n)
        {
          sp = _frames[n]._pixels;
          for (j=0; j<_size; j++)
            gray[*sp++] = 1;
        }
        for (i=0; i<256; i++)
        if (gray[i] == 0)
        {
          _trns[0] = 0;
          _trns[1] = i;
          _trnssize = 2;
          break;
        }
      }
    }

    for (size_t n = 0; n < _frames.size(); ++n)
    {
      _frames[n]._colorType = coltype;
      _frames[n]._paletteSize = _palsize;
      _frames[n]._transparencySize = _trnssize;

      if (_palsize != 0)
        memcpy(_frames[n]._palette, _palette, _palsize);

      if (_trnssize != 0)
        memcpy(_frames[n]._transparency, _trns, _trnssize);
    }

    return coltype;
  }

  bool APNGAsm::save(const std::string &outputPath, unsigned char coltype, unsigned int first, unsigned int loops)
  {
    unsigned int    j, k;
    unsigned int    has_tcolor = 0;
    unsigned int    tcolor = 0;
    unsigned int    i, rowbytes, imagesize;
    unsigned int    idat_size, zbuf_size, zsize;
    unsigned char * zbuf;
    FILE*           f;
    unsigned char   png_sign[8] = {137,  80,  78,  71,  13,  10,  26,  10};
    unsigned char   png_Software[27] = { 83, 111, 102, 116, 119, 97, 114, 101, '\0',
                                         65,  80,  78,  71,  32, 65, 115, 115, 101,
                                        109,  98, 108, 101, 114, 32,  50,  46,  56};

    unsigned int bpp = 1;
    if (coltype == 2)
      bpp = 3;
    else
    if (coltype == 4)
      bpp = 2;
    else
    if (coltype == 6)
      bpp = 4;

    if (coltype == 0)
    {
      if (_trnssize)
      {
        has_tcolor = 1;
        tcolor = _trns[1];
      }
    }
    else
    if (coltype == 2)
    {
      if (_trnssize)
      {
        has_tcolor = 1;
        tcolor = (((_trns[5]<<8)+_trns[3])<<8)+_trns[1];
      }
    }
    else
    if (coltype == 3)
    {
      for (i=0; i<_trnssize; i++)
      if (_trns[i] == 0)
      {
        has_tcolor = 1;
        tcolor = i;
        break;
      }
    }
    else
    {
      has_tcolor = 1;
      tcolor = 0;
    }

    rowbytes  = _width * bpp;
    imagesize = rowbytes * _height;

    unsigned char * temp  = (unsigned char *)malloc(imagesize);
    unsigned char * over1 = (unsigned char *)malloc(imagesize);
    unsigned char * over2 = (unsigned char *)malloc(imagesize);
    unsigned char * over3 = (unsigned char *)malloc(imagesize);
    unsigned char * rows  = (unsigned char *)malloc((rowbytes + 1) * _height);

    if (!temp || !over1 || !over2 || !over3 || !rows)
      return false;

    if ((f = fopen(outputPath.c_str(), "wb")) != 0)
    {
      struct IHDR
      {
        unsigned int    mWidth;
        unsigned int    mHeight;
        unsigned char   mDepth;
        unsigned char   mColorType;
        unsigned char   mCompression;
        unsigned char   mFilterMethod;
        unsigned char   mInterlaceMethod;
      } ihdr = { swap32(_width), swap32(_height), 8, coltype, 0, 0, 0 };

      struct acTL
      {
        unsigned int    mFrameCount;
        unsigned int    mLoopCount;
      } actl = { swap32(_frames.size()-first), swap32(loops) };

      struct fcTL
      {
        unsigned int    mSeq;
        unsigned int    mWidth;
        unsigned int    mHeight;
        unsigned int    mXOffset;
        unsigned int    mYOffset;
        unsigned short  mDelayNum;
        unsigned short  mDelayDen;
        unsigned char   mDisposeOp;
        unsigned char   mBlendOp;
      } fctl;

      fwrite(png_sign, 1, 8, f);

      write_chunk(f, "IHDR", (unsigned char *)(&ihdr), 13);

      if (_frames.size() > 1)
        write_chunk(f, "acTL", (unsigned char *)(&actl), 8);
      else
        first = 0;

      if (_palsize > 0)
        write_chunk(f, "PLTE", (unsigned char *)(&_palette), _palsize*3);

      if (_trnssize > 0)
        write_chunk(f, "tRNS", _trns, _trnssize);

      _op_zstream1.data_type = Z_BINARY;
      _op_zstream1.zalloc = Z_NULL;
      _op_zstream1.zfree = Z_NULL;
      _op_zstream1.opaque = Z_NULL;
      deflateInit2(&_op_zstream1, Z_BEST_SPEED+1, 8, 15, 8, Z_DEFAULT_STRATEGY);

      _op_zstream2.data_type = Z_BINARY;
      _op_zstream2.zalloc = Z_NULL;
      _op_zstream2.zfree = Z_NULL;
      _op_zstream2.opaque = Z_NULL;
      deflateInit2(&_op_zstream2, Z_BEST_SPEED+1, 8, 15, 8, Z_FILTERED);

      _fin_zstream.data_type = Z_BINARY;
      _fin_zstream.zalloc = Z_NULL;
      _fin_zstream.zfree = Z_NULL;
      _fin_zstream.opaque = Z_NULL;
      deflateInit2(&_fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, Z_DEFAULT_STRATEGY);

      idat_size = (rowbytes + 1) * _height;
      zbuf_size = idat_size + ((idat_size + 7) >> 3) + ((idat_size + 63) >> 6) + 11;

      zbuf = (unsigned char *)malloc(zbuf_size);
      _op_zbuf1 = (unsigned char *)malloc(zbuf_size);
      _op_zbuf2 = (unsigned char *)malloc(zbuf_size);
      _row_buf = (unsigned char *)malloc(rowbytes + 1);
      _sub_row = (unsigned char *)malloc(rowbytes + 1);
      _up_row = (unsigned char *)malloc(rowbytes + 1);
      _avg_row = (unsigned char *)malloc(rowbytes + 1);
      _paeth_row = (unsigned char *)malloc(rowbytes + 1);

      if (zbuf && _op_zbuf1 && _op_zbuf2 && _row_buf && _sub_row && _up_row && _avg_row && _paeth_row)
      {
        _row_buf[0] = 0;
        _sub_row[0] = 1;
        _up_row[0] = 2;
        _avg_row[0] = 3;
        _paeth_row[0] = 4;
      }
      else
        return false;

      unsigned int x0 = 0;
      unsigned int y0 = 0;
      unsigned int w0 = _width;
      unsigned int h0 = _height;
      unsigned char bop = 0;
      unsigned char dop = 0;
      _next_seq_num = 0;

      for (j=0; j<6; j++)
        _op[j].valid = 0;
      deflate_rect_op(_frames[0]._pixels, x0, y0, w0, h0, bpp, rowbytes, zbuf_size, 0);
      deflate_rect_fin(zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, 0);

      if (first)
      {
        write_IDATs(f, 0, zbuf, zsize, idat_size);

        for (j=0; j<6; j++)
          _op[j].valid = 0;
        deflate_rect_op(_frames[1]._pixels, x0, y0, w0, h0, bpp, rowbytes, zbuf_size, 0);
        deflate_rect_fin(zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, 0);
      }

      for (size_t n = first; n < _frames.size()-1; ++n)
      {
        unsigned int op_min;
        int          op_best;

        for (j=0; j<6; j++)
          _op[j].valid = 0;

        /* dispose = none */
        get_rect(_width, _height, _frames[n]._pixels, _frames[n+1]._pixels, over1, bpp, rowbytes, zbuf_size, has_tcolor, tcolor, 0);

        /* dispose = background */
        if (has_tcolor)
        {
          memcpy(temp, _frames[n]._pixels, imagesize);
          if (coltype == 2)
            for (j=0; j<h0; j++)
              for (k=0; k<w0; k++)
                memcpy(temp + ((j+y0)*_width + (k+x0))*3, &tcolor, 3);
          else
            for (j=0; j<h0; j++)
              memset(temp + ((j+y0)*_width + x0)*bpp, tcolor, w0*bpp);

          get_rect(_width, _height, temp, _frames[n+1]._pixels, over2, bpp, rowbytes, zbuf_size, has_tcolor, tcolor, 1);
        }

        /* dispose = previous */
        if (n > first)
          get_rect(_width, _height, _frames[n-1]._pixels, _frames[n+1]._pixels, over3, bpp, rowbytes, zbuf_size, has_tcolor, tcolor, 2);

        op_min = _op[0].size;
        op_best = 0;
        for (j=1; j<6; j++)
        {
          if (_op[j].size < op_min && _op[j].valid)
          {
            op_min = _op[j].size;
            op_best = j;
          }
        }

        dop = op_best >> 1;

        fctl.mSeq       = swap32(_next_seq_num++);
        fctl.mWidth     = swap32(w0);
        fctl.mHeight    = swap32(h0);
        fctl.mXOffset   = swap32(x0);
        fctl.mYOffset   = swap32(y0);
        fctl.mDelayNum  = swap16(_frames[n]._delayNum);
        fctl.mDelayDen  = swap16(_frames[n]._delayDen);
        fctl.mDisposeOp = dop;
        fctl.mBlendOp   = bop;
        write_chunk(f, "fcTL", (unsigned char *)(&fctl), 26);

        write_IDATs(f, n, zbuf, zsize, idat_size);

        /* process apng dispose - begin */
        if (dop == 1)
        {
          if (coltype == 2)
            for (j=0; j<h0; j++)
              for (k=0; k<w0; k++)
                memcpy(_frames[n]._pixels + ((j+y0)*_width + (k+x0))*3, &tcolor, 3);
          else
            for (j=0; j<h0; j++)
              memset(_frames[n]._pixels + ((j+y0)*_width + x0)*bpp, tcolor, w0*bpp);
        }
        else
        if (dop == 2)
        {
          for (j=0; j<h0; j++)
            memcpy(_frames[n]._pixels + ((j+y0)*_width + x0)*bpp, _frames[n-1]._pixels + ((j+y0)*_width + x0)*bpp, w0*bpp);
        }
        /* process apng dispose - end */

        x0 = _op[op_best].x;
        y0 = _op[op_best].y;
        w0 = _op[op_best].w;
        h0 = _op[op_best].h;
        bop = op_best & 1;

        deflate_rect_fin(zbuf, &zsize, bpp, rowbytes, rows, zbuf_size, op_best);
      }

      if (_frames.size() > 1)
      {
        fctl.mSeq       = swap32(_next_seq_num++);
        fctl.mWidth     = swap32(w0);
        fctl.mHeight    = swap32(h0);
        fctl.mXOffset   = swap32(x0);
        fctl.mYOffset   = swap32(y0);
        fctl.mDelayNum  = swap16(_frames[_frames.size()-1]._delayNum);
        fctl.mDelayDen  = swap16(_frames[_frames.size()-1]._delayDen);
        fctl.mDisposeOp = 0;
        fctl.mBlendOp   = bop;
        write_chunk(f, "fcTL", (unsigned char *)(&fctl), 26);
      }

      write_IDATs(f, _frames.size()-1, zbuf, zsize, idat_size);

      write_chunk(f, "tEXt", png_Software, 27);
      write_chunk(f, "IEND", 0, 0);
      fclose(f);

      free(zbuf);
      free(_op_zbuf1);
      free(_op_zbuf2);
      free(_row_buf);
      free(_sub_row);
      free(_up_row);
      free(_avg_row);
      free(_paeth_row);
    }
    else
      return false;

    free(temp);
    free(over1);
    free(over2);
    free(over3);
    free(rows);

    return true;
  }

  void APNGAsm::process_rect(unsigned char * row, int rowbytes, int bpp, int stride, int h, unsigned char * rows)
  {
    int i, j, v;
    int a, b, c, pa, pb, pc, p;
    unsigned char * prev = NULL;
    unsigned char * dp  = rows;
    unsigned char * out;

    for (j=0; j<h; j++)
    {
      unsigned int    sum = 0;
      unsigned char * best_row = _row_buf;
      unsigned int    mins = ((unsigned int)(-1)) >> 1;

      out = _row_buf+1;
      for (i=0; i<rowbytes; i++)
      {
        v = out[i] = row[i];
        sum += (v < 128) ? v : 256 - v;
      }
      mins = sum;

      sum = 0;
      out = _sub_row+1;
      for (i=0; i<bpp; i++)
      {
        v = out[i] = row[i];
        sum += (v < 128) ? v : 256 - v;
      }
      for (i=bpp; i<rowbytes; i++)
      {
        v = out[i] = row[i] - row[i-bpp];
        sum += (v < 128) ? v : 256 - v;
        if (sum > mins) break;
      }
      if (sum < mins)
      {
        mins = sum;
        best_row = _sub_row;
      }

      if (prev)
      {
        sum = 0;
        out = _up_row+1;
        for (i=0; i<rowbytes; i++)
        {
          v = out[i] = row[i] - prev[i];
          sum += (v < 128) ? v : 256 - v;
          if (sum > mins) break;
        }
        if (sum < mins)
        {
          mins = sum;
          best_row = _up_row;
        }

        sum = 0;
        out = _avg_row+1;
        for (i=0; i<bpp; i++)
        {
          v = out[i] = row[i] - prev[i]/2;
          sum += (v < 128) ? v : 256 - v;
        }
        for (i=bpp; i<rowbytes; i++)
        {
          v = out[i] = row[i] - (prev[i] + row[i-bpp])/2;
          sum += (v < 128) ? v : 256 - v;
          if (sum > mins) break;
        }
        if (sum < mins)
        {
          mins = sum;
          best_row = _avg_row;
        }

        sum = 0;
        out = _paeth_row+1;
        for (i=0; i<bpp; i++)
        {
          v = out[i] = row[i] - prev[i];
          sum += (v < 128) ? v : 256 - v;
        }
        for (i=bpp; i<rowbytes; i++)
        {
          a = row[i-bpp];
          b = prev[i];
          c = prev[i-bpp];
          p = b - c;
          pc = a - c;
          pa = abs(p);
          pb = abs(pc);
          pc = abs(p + pc);
          p = (pa <= pb && pa <=pc) ? a : (pb <= pc) ? b : c;
          v = out[i] = row[i] - p;
          sum += (v < 128) ? v : 256 - v;
          if (sum > mins) break;
        }
        if (sum < mins)
        {
          best_row = _paeth_row;
        }
      }

      if (rows == NULL)
      {
        // deflate_rect_op()
        _op_zstream1.next_in = _row_buf;
        _op_zstream1.avail_in = rowbytes + 1;
        deflate(&_op_zstream1, Z_NO_FLUSH);

        _op_zstream2.next_in = best_row;
        _op_zstream2.avail_in = rowbytes + 1;
        deflate(&_op_zstream2, Z_NO_FLUSH);
      }
      else
      {
        // deflate_rect_fin()
        memcpy(dp, best_row, rowbytes+1);
        dp += rowbytes+1;
      }

      prev = row;
      row += stride;
    }
  }

  void APNGAsm::deflate_rect_fin(unsigned char * zbuf, unsigned int * zsize, int bpp, int stride, unsigned char * rows, int zbuf_size, int n)
  {
    unsigned char * row  = _op[n].p + _op[n].y*stride + _op[n].x*bpp;
    int rowbytes = _op[n].w*bpp;

    if (_op[n].filters == 0)
    {
      deflateInit2(&_fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, Z_DEFAULT_STRATEGY);
      unsigned char * dp  = rows;
      for (int j=0; j<_op[n].h; j++)
      {
        *dp++ = 0;
        memcpy(dp, row, rowbytes);
        dp += rowbytes;
        row += stride;
      }
    }
    else
    {
      deflateInit2(&_fin_zstream, Z_BEST_COMPRESSION, 8, 15, 8, Z_FILTERED);
      process_rect(row, rowbytes, bpp, stride, _op[n].h, rows);
    }

    _fin_zstream.next_out = zbuf;
    _fin_zstream.avail_out = zbuf_size;
    _fin_zstream.next_in = rows;
    _fin_zstream.avail_in = _op[n].h*(rowbytes + 1);
    deflate(&_fin_zstream, Z_FINISH);
    *zsize = _fin_zstream.total_out;
    deflateReset(&_fin_zstream);
  }

  void APNGAsm::deflate_rect_op(unsigned char *pdata, int x, int y, int w, int h, int bpp, int stride, int zbuf_size, int n)
  {
    unsigned char * row  = pdata + y*stride + x*bpp;
    int rowbytes = w * bpp;

    _op_zstream1.data_type = Z_BINARY;
    _op_zstream1.next_out = _op_zbuf1;
    _op_zstream1.avail_out = zbuf_size;

    _op_zstream2.data_type = Z_BINARY;
    _op_zstream2.next_out = _op_zbuf2;
    _op_zstream2.avail_out = zbuf_size;

    process_rect(row, rowbytes, bpp, stride, h, NULL);

    deflate(&_op_zstream1, Z_FINISH);
    deflate(&_op_zstream2, Z_FINISH);
    _op[n].p = pdata;
    if (_op_zstream1.total_out < _op_zstream2.total_out)
    {
      _op[n].size = _op_zstream1.total_out;
      _op[n].filters = 0;
    }
    else
    {
      _op[n].size = _op_zstream2.total_out;
      _op[n].filters = 1;
    }
    _op[n].x = x;
    _op[n].y = y;
    _op[n].w = w;
    _op[n].h = h;
    _op[n].valid = 1;
    deflateReset(&_op_zstream1);
    deflateReset(&_op_zstream2);
  }

  void APNGAsm::get_rect(unsigned int w, unsigned int h, unsigned char *pimage1, unsigned char *pimage2, unsigned char *ptemp, unsigned int bpp, unsigned int stride, int zbuf_size, unsigned int has_tcolor, unsigned int tcolor, int n)
  {
    unsigned int   i, j, x0, y0, w0, h0;
    unsigned int   x_min = w-1;
    unsigned int   y_min = h-1;
    unsigned int   x_max = 0;
    unsigned int   y_max = 0;
    unsigned int   diffnum = 0;
    unsigned int   over_is_possible = 1;

    if (!has_tcolor)
      over_is_possible = 0;

    if (bpp == 1)
    {
      unsigned char *pa = pimage1;
      unsigned char *pb = pimage2;
      unsigned char *pc = ptemp;

      for (j=0; j<h; j++)
      for (i=0; i<w; i++)
      {
        unsigned char c = *pb++;
        if (*pa++ != c)
        {
          diffnum++;
          if (has_tcolor && c == tcolor) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c = tcolor;

        *pc++ = c;
      }
    }
    else
    if (bpp == 2)
    {
      unsigned short *pa = (unsigned short *)pimage1;
      unsigned short *pb = (unsigned short *)pimage2;
      unsigned short *pc = (unsigned short *)ptemp;

      for (j=0; j<h; j++)
      for (i=0; i<w; i++)
      {
        unsigned int c1 = *pa++;
        unsigned int c2 = *pb++;
        if ((c1 != c2) && ((c1>>8) || (c2>>8)))
        {
          diffnum++;
          if ((c2 >> 8) != 0xFF) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c2 = 0;

        *pc++ = c2;
      }
    }
    else
    if (bpp == 3)
    {
      unsigned char *pa = pimage1;
      unsigned char *pb = pimage2;
      unsigned char *pc = ptemp;

      for (j=0; j<h; j++)
      for (i=0; i<w; i++)
      {
        unsigned int c1 = (pa[2]<<16) + (pa[1]<<8) + pa[0];
        unsigned int c2 = (pb[2]<<16) + (pb[1]<<8) + pb[0];
        if (c1 != c2)
        {
          diffnum++;
          if (has_tcolor && c2 == tcolor) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c2 = tcolor;

        memcpy(pc, &c2, 3);
        pa += 3;
        pb += 3;
        pc += 3;
      }
    }
    else
    if (bpp == 4)
    {
      unsigned int *pa = (unsigned int *)pimage1;
      unsigned int *pb = (unsigned int *)pimage2;
      unsigned int *pc = (unsigned int *)ptemp;

      for (j=0; j<h; j++)
      for (i=0; i<w; i++)
      {
        unsigned int c1 = *pa++;
        unsigned int c2 = *pb++;
        if ((c1 != c2) && ((c1>>24) || (c2>>24)))
        {
          diffnum++;
          if ((c2 >> 24) != 0xFF) over_is_possible = 0;
          if (i<x_min) x_min = i;
          if (i>x_max) x_max = i;
          if (j<y_min) y_min = j;
          if (j>y_max) y_max = j;
        }
        else
          c2 = 0;

        *pc++ = c2;
      }
    }

    if (diffnum == 0)
    {
      x0 = y0 = 0;
      w0 = h0 = 1;
    }
    else
    {
      x0 = x_min;
      y0 = y_min;
      w0 = x_max-x_min+1;
      h0 = y_max-y_min+1;
    }

    deflate_rect_op(pimage2, x0, y0, w0, h0, bpp, stride, zbuf_size, n*2);

    if (over_is_possible)
      deflate_rect_op(ptemp, x0, y0, w0, h0, bpp, stride, zbuf_size, n*2+1);
  }

  void APNGAsm::write_chunk(FILE * f, const char * name, unsigned char * data, unsigned int length)
  {
    unsigned int crc = crc32(0, Z_NULL, 0);
    unsigned int len = swap32(length);

    fwrite(&len, 1, 4, f);
    fwrite(name, 1, 4, f);
    crc = crc32(crc, (const Bytef *)name, 4);

    if (memcmp(name, "fdAT", 4) == 0)
    {
      unsigned int seq = swap32(_next_seq_num++);
      fwrite(&seq, 1, 4, f);
      crc = crc32(crc, (const Bytef *)(&seq), 4);
      length -= 4;
    }

    if (data != NULL && length > 0)
    {
      fwrite(data, 1, length, f);
      crc = crc32(crc, data, length);
    }

    crc = swap32(crc);
    fwrite(&crc, 1, 4, f);
  }

  void APNGAsm::write_IDATs(FILE * f, int n, unsigned char * data, unsigned int length, unsigned int idat_size)
  {
    unsigned int z_cmf = data[0];
    if ((z_cmf & 0x0f) == 8 && (z_cmf & 0xf0) <= 0x70)
    {
      if (length >= 2)
      {
        unsigned int z_cinfo = z_cmf >> 4;
        unsigned int half_z_window_size = 1 << (z_cinfo + 7);
        while (idat_size <= half_z_window_size && half_z_window_size >= 256)
        {
          z_cinfo--;
          half_z_window_size >>= 1;
        }
        z_cmf = (z_cmf & 0x0f) | (z_cinfo << 4);
        if (data[0] != (unsigned char)z_cmf)
        {
          data[0] = (unsigned char)z_cmf;
          data[1] &= 0xe0;
          data[1] += (unsigned char)(0x1f - ((z_cmf << 8) + data[1]) % 0x1f);
        }
      }
    }

    while (length > 0)
    {
      unsigned int ds = length;
      if (ds > 32768)
        ds = 32768;

      if (n == 0)
        write_chunk(f, "IDAT", data, ds);
      else
        write_chunk(f, "fdAT", data, ds+4);

      data += ds;
      length -= ds;
    }
  }
#endif

#ifdef APNG_READ_SUPPORTED
  void info_fn(png_structp png_ptr, png_infop info_ptr)
  {
    png_set_expand(png_ptr);
    png_set_strip_16(png_ptr);
    png_set_gray_to_rgb(png_ptr);
    png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
    (void)png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);
  }

  void row_fn(png_structp png_ptr, png_bytep new_row, png_uint_32 row_num, int pass)
  {
    APNGFrame * frame = (APNGFrame *)png_get_progressive_ptr(png_ptr);
    png_progressive_combine_row(png_ptr, frame->_rows[row_num], new_row);
  }

  const std::vector<APNGFrame>& APNGAsm::disassemble(const std::string &filePath)
  {
    unsigned int   i, j, id;
    unsigned int   w, h;
    unsigned int * pi;
    unsigned char  header[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char  footer[12] = {0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130};
    CHUNK chunk_ihdr;
    CHUNK chunk;

    reset();

    FILE * f;
    if ((f = fopen(filePath.c_str(), "rb")) != 0)
    {
      unsigned char sig[8];
      unsigned int  w0, h0, x0, y0;
      unsigned int  delay_num, delay_den, dop, bop, rowbytes, imagesize;
      unsigned int  flag_actl = 0;
      unsigned int  flag_fctl = 0;
      unsigned int  flag_idat = 0;
      unsigned int  flag_info = 0;
      unsigned int  num_frames = 1;

      if (fread(sig, 1, 8, f) == 8 && memcmp(sig, header, 8) == 0)
      {
        id = read_chunk(f, &chunk_ihdr);

        if (id == id_IHDR && chunk_ihdr.size == 25)
        {
          pi = (unsigned int *)chunk_ihdr.p;
          w0 = w = swap32(pi[2]);
          h0 = h = swap32(pi[3]);
          x0 = 0;
          y0 = 0;
          delay_num = 1;
          delay_den = 10;
          dop = 0;
          bop = 0;
          rowbytes = w * 4;
          imagesize = h * rowbytes;

          APNGFrame frameRaw;
          APNGFrame frameCur;
          APNGFrame frameNext;

          frameRaw._pixels = new unsigned char[imagesize];
          frameRaw._rows = new png_bytep[h * sizeof(png_bytep)];
          for (j=0; j<h; ++j)
            frameRaw._rows[j] = frameRaw._pixels + j * rowbytes;

          frameCur._width = w;
          frameCur._height = h;
          frameCur._colorType = 6;
          frameCur._pixels = new unsigned char[imagesize];
          frameCur._rows = new png_bytep[h * sizeof(png_bytep)];
          for (j=0; j<h; ++j)
            frameCur._rows[j] = frameCur._pixels + j * rowbytes;

          png_structp png_ptr  = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
          png_infop   info_ptr = png_create_info_struct(png_ptr);
          setjmp(png_jmpbuf(png_ptr));
          png_set_progressive_read_fn(png_ptr, (void *)&frameRaw, info_fn, row_fn, NULL);
          png_process_data(png_ptr, info_ptr, &header[0], 8);
          png_process_data(png_ptr, info_ptr, chunk_ihdr.p, chunk_ihdr.size);
          flag_info = 0;

          while ( !feof(f) )
          {
            id = read_chunk(f, &chunk);
            pi = (unsigned int *)chunk.p;

            if (id == id_acTL)
            {
              flag_actl = 1;
              num_frames = swap32(pi[2]);
              delete[] chunk.p;
            }
            else
            if (id == id_fcTL)
            {
              if (flag_fctl)
              {
                png_process_data(png_ptr, info_ptr, &footer[0], 12);
                png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

                frameNext._pixels = new unsigned char[imagesize];
                frameNext._rows = new png_bytep[h * sizeof(png_bytep)];
                for (j=0; j<h; ++j)
                  frameNext._rows[j] = frameNext._pixels + j * rowbytes;

                if (dop == 2)
                  memcpy(frameNext._pixels, frameCur._pixels, imagesize);

                compose_frame(frameCur._rows, frameRaw._rows, bop, x0, y0, w0, h0);
                frameCur._delayNum = delay_num;
                frameCur._delayDen = delay_den;
                _frames.push_back(frameCur);

                if (dop != 2)
                {
                  memcpy(frameNext._pixels, frameCur._pixels, imagesize);
                  if (dop == 1)
                    for (j=0; j<h0; j++)
                      memset(frameNext._rows[y0 + j] + x0*4, 0, w0*4);
                }
                frameCur._pixels = frameNext._pixels;
                frameCur._rows = frameNext._rows;

                memcpy(chunk_ihdr.p + 8, chunk.p + 12, 8);
                recalc_crc(chunk_ihdr.p, chunk_ihdr.size);

                png_ptr  = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
                info_ptr = png_create_info_struct(png_ptr);
                setjmp(png_jmpbuf(png_ptr));
                png_set_progressive_read_fn(png_ptr, (void *)&frameRaw, info_fn, row_fn, NULL);
                png_process_data(png_ptr, info_ptr, &header[0], 8);
                png_process_data(png_ptr, info_ptr, chunk_ihdr.p, chunk_ihdr.size);
                flag_info = 0;
              }

              w0 = swap32(pi[3]);
              h0 = swap32(pi[4]);
              x0 = swap32(pi[5]);
              y0 = swap32(pi[6]);
              delay_num = chunk.p[28]*256 + chunk.p[29];
              delay_den = chunk.p[30]*256 + chunk.p[31];
              dop = chunk.p[32];
              bop = chunk.p[33];
              if (!flag_fctl)
              {
                bop = 0;
                if (dop == 2)
                  dop = 1;
              }
              flag_fctl = 1;
              delete[] chunk.p;
            }
            else
            if (id == id_IDAT)
            {
              flag_idat = 1;
              if (flag_fctl || !flag_actl)
              {
                if (!flag_info)
                {
                  flag_info = 1;
                  for (i=0; i<_info_chunks.size(); ++i)
                    png_process_data(png_ptr, info_ptr, _info_chunks[i].p, _info_chunks[i].size);
                }
                png_process_data(png_ptr, info_ptr, chunk.p, chunk.size);
              }
              delete[] chunk.p;
            }
            else
            if (id == id_fdAT)
            {
              flag_idat = 1;
              if (!flag_info)
              {
                flag_info = 1;
                for (i=0; i<_info_chunks.size(); ++i)
                  png_process_data(png_ptr, info_ptr, _info_chunks[i].p, _info_chunks[i].size);
              }
              pi[1] = swap32(chunk.size - 16);
              pi[2] = id_IDAT;
              recalc_crc(chunk.p + 4, chunk.size - 4);
              png_process_data(png_ptr, info_ptr, chunk.p + 4, chunk.size - 4);
              delete[] chunk.p;
            }
            else
            if (id == id_IEND)
            {
              png_process_data(png_ptr, info_ptr, &footer[0], 12);

              compose_frame(frameCur._rows, frameRaw._rows, bop, x0, y0, w0, h0);
              frameCur._delayNum = delay_num;
              frameCur._delayDen = delay_den;
              _frames.push_back(frameCur);
              delete[] chunk.p;
              break;
            }
            else
            if (notabc(chunk.p[4]) || notabc(chunk.p[5]) || notabc(chunk.p[6]) || notabc(chunk.p[7]))
            {
              delete[] chunk.p;
              break;
            }
            else
            {
              if (!flag_idat)
                _info_chunks.push_back(chunk);
              else
                delete[] chunk.p;
            }
          }
          delete[] frameRaw._rows;
          delete[] frameRaw._pixels;
          png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        }
      }
      fclose(f);

      for (i=0; i<_info_chunks.size(); ++i)
        delete[] _info_chunks[i].p;

      _info_chunks.clear();

      delete[] chunk_ihdr.p;
    }
    return _frames;
  }

  void APNGAsm::compose_frame(unsigned char ** rows_dst, unsigned char ** rows_src, unsigned char bop, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
  {
    unsigned int  i, j;
    int u, v, al;

    for (j=0; j<h; j++)
    {
      unsigned char * sp = rows_src[j];
      unsigned char * dp = rows_dst[j+y] + x*4;

      if (bop == 0)
        memcpy(dp, sp, w*4);
      else
      for (i=0; i<w; i++, sp+=4, dp+=4)
      {
        if (sp[3] == 255)
          memcpy(dp, sp, 4);
        else
        if (sp[3] != 0)
        {
          if (dp[3] != 0)
          {
            u = sp[3]*255;
            v = (255-sp[3])*dp[3];
            al = 255*255-(255-sp[3])*(255-dp[3]);
            dp[0] = (sp[0]*u + dp[0]*v)/al;
            dp[1] = (sp[1]*u + dp[1]*v)/al;
            dp[2] = (sp[2]*u + dp[2]*v)/al;
            dp[3] = al/255;
          }
          else
            memcpy(dp, sp, 4);
        }
      }
    }
  }

  unsigned int APNGAsm::read_chunk(FILE * f, CHUNK * pChunk)
  {
    unsigned int len;
    if (fread(&len, 4, 1, f) == 1)
    {
      pChunk->size = swap32(len) + 12;
      pChunk->p = new unsigned char[pChunk->size];
      unsigned int * pi = (unsigned int *)pChunk->p;
      pi[0] = len;
      if (fread(pChunk->p + 4, pChunk->size - 4, 1, f) == 1)
        return pi[1];
    }
    return 0;
  }

  void APNGAsm::recalc_crc(unsigned char * p, unsigned int size)
  {
    unsigned int crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, p + 4, size - 8);
    crc = swap32(crc);
    memcpy(p + size - 4, &crc, 4);
  }

  // Save png files.
  bool APNGAsm::savePNGs(const std::string& outputDir) const
  {
    const int count = _frames.size();
    for(int i = 0;  i < count;  ++i)
    {
      const std::string outputPath = _pListener->onCreatePngPath(outputDir, i);

      if( !_pListener->onPreSave(outputPath) )
        return false;

      if( !_frames[i].save(outputPath) )
        return false;

      _pListener->onPostSave(outputPath);
    }
    return true;
  }
#endif

} // namespace apngasm
