// Stub MINIMO de Arduino_GFX para probar ScreenS3.cpp en el PC (WSL).
//
// Las firmas replican EXACTAMENTE las del repo moononournation/Arduino_GFX
// (comprobadas contra el fuente antes de escribir el driver), de modo que si
// ScreenS3.cpp compila contra este stub, tambien encaja con la libreria real:
//   Arduino_ESP32SPI(dc, cs, sck, mosi, miso, spi_num, is_shared_interface)
//   Arduino_ST7789(bus, rst, r, ips, w, h, coff1, roff1, coff2, roff2)
//   virtual bool begin(int32_t speed)
//   void draw16bitRGBBitmap(int16_t, int16_t, uint16_t*, int16_t, int16_t)
#ifndef _STUB_ARDUINO_GFX_H
#define _STUB_ARDUINO_GFX_H

#include <cstdint>

#define GFX_NOT_DEFINED (-1)
#define FSPI 0

// Contadores que inspecciona el test para comprobar que el motor de celdas
// solo vuelca lo que cambia.
extern long g_blitCount;
extern long g_fillScreenCount;

class Arduino_DataBus
{
public:
    virtual ~Arduino_DataBus() {}
};

class Arduino_ESP32SPI : public Arduino_DataBus
{
public:
    Arduino_ESP32SPI(int8_t dc = GFX_NOT_DEFINED, int8_t cs = GFX_NOT_DEFINED,
                     int8_t sck = GFX_NOT_DEFINED, int8_t mosi = GFX_NOT_DEFINED,
                     int8_t miso = GFX_NOT_DEFINED, uint8_t spi_num = FSPI,
                     bool is_shared_interface = true)
        : _dc(dc), _cs(cs), _sck(sck), _mosi(mosi), _miso(miso),
          _spi_num(spi_num), _shared(is_shared_interface) {}
    int8_t _dc, _cs, _sck, _mosi, _miso;
    uint8_t _spi_num;
    bool _shared;
};

// Cuenta los pixeles empujados por el camino de bitmap (writeRepeat), para
// poder comprobar en el test que el logo cubre EXACTAMENTE la pantalla.
extern unsigned long g_logoPixels;

// Arduino_TFT existe de verdad en la libreria y es donde viven writeAddrWindow()
// y writeRepeat(); el driver apunta a este tipo por eso mismo. El stub reproduce
// la jerarquia real: Arduino_ST7789 -> Arduino_TFT -> Arduino_GFX.
class Arduino_GFX
{
public:
    Arduino_GFX(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h), _width(w), _height(h) {}
    virtual ~Arduino_GFX() {}
    virtual bool begin(int32_t speed = GFX_NOT_DEFINED) { (void)speed; return true; }
    virtual void setRotation(uint8_t r)
    {
        // Mismo criterio que Arduino_GFX::setRotation real: las rotaciones
        // impares intercambian ancho y alto.
        _rotation = r & 7;
        if (_rotation & 1) { _width = HEIGHT; _height = WIDTH; }
        else               { _width = WIDTH;  _height = HEIGHT; }
    }
    void fillScreen(uint16_t) { g_fillScreenCount++; }
    void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bmp, int16_t w, int16_t h)
    {
        (void)x; (void)y; (void)bmp; (void)w; (void)h;
        g_blitCount++;
    }
    virtual void startWrite() {}
    virtual void endWrite() {}
    int16_t width() const { return _width; }
    int16_t height() const { return _height; }

protected:
    int16_t WIDTH, HEIGHT, _width, _height;
    uint8_t _rotation = 0;
};

class Arduino_TFT : public Arduino_GFX
{
public:
    Arduino_TFT(int16_t w, int16_t h) : Arduino_GFX(w, h) {}
    virtual void writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h)
    {
        (void)x; (void)y; (void)w; (void)h;
    }
    virtual void writeRepeat(uint16_t color, uint32_t len)
    {
        (void)color;
        g_logoPixels += len;
    }
};

class Arduino_ST7789 : public Arduino_TFT
{
public:
    Arduino_ST7789(Arduino_DataBus *bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0,
                   bool ips = false, int16_t w = 240, int16_t h = 320,
                   uint8_t col_offset1 = 0, uint8_t row_offset1 = 0,
                   uint8_t col_offset2 = 0, uint8_t row_offset2 = 0)
        : Arduino_TFT(w, h), _bus(bus), _rst(rst), _ips(ips),
          _c1(col_offset1), _r1(row_offset1), _c2(col_offset2), _r2(row_offset2)
    {
        setRotation(r);
    }
    bool begin(int32_t speed = GFX_NOT_DEFINED) override { (void)speed; return true; }
    Arduino_DataBus *_bus;
    int8_t _rst;
    bool _ips;
    uint8_t _c1, _r1, _c2, _r2;
};

#endif
