#if !defined(IMAGE_HEADER)
#define IMAGE_HEADER

#include <FastLED.h>

class Image {
  public:
    Image(uint16_t width, uint16_t height, uint16_t colors = 0) 
      : _width(width), _height(height), _dataRGB(0), _dataI(0), _cmap(0) {
         if (colors > 0) {
          _dataI = (byte*)malloc(_width * _height);
          _cmap = (CRGB*)malloc(colors * sizeof(CRGB));
        } else {
          _dataRGB = (CRGB*)malloc(_width * _height * sizeof(CRGB));
        }
  }
  virtual ~Image() {  
    free(_dataRGB);
    free(_dataI);
    free(_cmap);
  }
  uint16_t getWidth() const { return _width; }
  uint16_t getHeight() const { return _height; }
  
  CRGB& getPixel(uint16_t x, uint16_t y) const {
    unsigned int idx = y * _width + x;
    if (_cmap) {
       return _cmap[_dataI[idx]];
    } else {
      return _dataRGB[idx];
    }
  }
  void setPixel(uint16_t x, uint16_t y, CRGB color) {
    if (_dataRGB) {
      unsigned int idx = y * _width + x;
      _dataRGB[idx] = color;
    }
  }
  void setPixel(uint16_t x, uint16_t y, byte color) {
    if (_dataI) {
      unsigned int idx = y * _width + x;
      if (idx < (_width * _height))
        _dataI[idx] = color;
    }
  }
  void setCmap(int index, CRGB color) {
    if (_cmap) 
    _cmap[index] = color;
  }
  
  CRGB* getDataRGB() const { 
    return _dataRGB; 
  }
  private:
  uint16_t _width;
  uint16_t _height;
  CRGB*    _dataRGB;
  byte*    _dataI;
  CRGB*    _cmap; 
};

#endif
