#pragma once

#ifndef TCODEC_INCLUDED
#define TCODEC_INCLUDED

#include "traster.h"

#undef DVAPI
#undef DVVAR
#ifdef TRASTERIMAGE_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//------------------------------------------------------------------------------

class DVAPI TRasterCodec {
public:
  TRasterCodec(const std::string &name) : m_name(name) {}
  virtual ~TRasterCodec() {}

  // virtual int getMaxCompressionSize(int size) = 0;
  // virtual void compress  (const TRasterP &inRas, int allocUnit, UCHAR**
  // outData, TINT32 &outDataSize) = 0;
  // virtual void decompress(const UCHAR* inData, TINT32 inDataSize, TRasterP
  // &outRas) = 0;

  // virtual int getHeaderSize() = 0;
  // virtual UCHAR *removeHeader(const UCHAR* inData, TINT32 inDataSize, TINT32
  // &outDataSize, int &lx, int &ly) = 0;

  static TRasterCodec *create(const std::string &name);

private:
  std::string m_name;
};

//------------------------------------------------------------------------------

class DVAPI TRasterCodecDummy final : public TRasterCodec {
public:
  TRasterCodecDummy(const std::string &name) : TRasterCodec(name) {}
  ~TRasterCodecDummy() {}

  // int getMaxCompressionSize(int size);
  // void compress(const TRasterP &inRas, int allocUnit, UCHAR** outData, TINT32
  // &outDataSize);
  // void decompress(const UCHAR* inData, TINT32 inDataSize, TRasterP &outRas);

  // int getHeaderSize();

  // UCHAR *removeHeader(const UCHAR* inData, TINT32 inDataSize, TINT32
  // &outDataSize, int &lx, int &ly);

  // static TRasterCodec *create(const std::string &name);
};

//------------------------------------------------------------------------------

class DVAPI TRasterCodecQTL final : public TRasterCodec {
public:
  TRasterCodecQTL(const std::string &name) : TRasterCodec(name) {}
  ~TRasterCodecQTL() {}

  // int getMaxCompressionSize(int size);
  // void compress  (const TRasterP &inRas, int allocUnit, UCHAR** outData,
  // TINT32 &outDataSize);
  // void decompress(const UCHAR* inData, TINT32 inDataSize, TRasterP &outRas);

  // UCHAR *removeHeader(const UCHAR* inData, TINT32 inDataSize, TINT32
  // &outDataSize, int &lx, int &ly);

  // int getHeaderSize();

  // static TRasterCodec *create(const std::string &name);
};

//------------------------------------------------------------------------------

/*class DVAPI TRasterCodecSnappy final : public TRasterCodec
{
public:

  TRasterCodecSnappy(const std::string &name, bool useCache);
  ~TRasterCodecSnappy();

  TRasterP compress(const TRasterP &inRas, int allocUnit, TINT32 &outDataSize);
  bool decompress(const UCHAR* inData, TINT32 inDataSize, TRasterP &outRas, bool
safeMode);
  void decompress(const TRasterP & compressedRas, TRasterP &outRas);

  void reset() { if (m_useCache) return; m_raster=TRasterGR8P(); }

private:

  TRasterGR8P m_raster;
  string m_cacheId;
  bool m_useCache;

private:

  UINT doCompress(const TRasterP &inRas, int allocUnit, TRasterGR8P& outRas);
};*/

//------------------------------------------------------------------------------

class DVAPI TRasterCodecLz4 : public TRasterCodec {
public:
  TRasterCodecLz4(const std::string &name, bool useCache);
  ~TRasterCodecLz4();

  TRasterP compress(const TRasterP &inRas, int allocUnit, TINT32 &outDataSize);
  bool decompress(const UCHAR *inData, TINT32 inDataSize, TRasterP &outRas,
                  bool safeMode);
  void decompress(const TRasterP &compressedRas, TRasterP &outRas);

  void reset() {
    if (m_useCache) return;
    m_raster = TRasterGR8P();
  }

private:
  TRasterGR8P m_raster;
  std::string m_cacheId;
  bool m_useCache;

private:
  UINT doCompress(const TRasterP &inRas, int allocUnit, TRasterGR8P &outRas);
};

//------------------------------------------------------------------------------

//! What TRasterCodecLZO compresses with: an injected service
//! (doc/nexttoonz_core_noqt.md, 8d-0). In the build with Qt the built-in one
//! runs the lzocompress and lzodecompress helpers next to the executable,
//! as it always has (tcodec_qt.cpp); without Qt there is none, and the codec
//! throws a TException saying LZO is not available. Step 13 is to link the
//! LZO library instead.
struct TLzoService {
  //! Compresses src into dst; false if that failed.
  bool (*compress)(const std::string &src, std::string &dst);
  //! Decompresses src into dst, which must come out dstSize bytes long;
  //! false if that failed.
  bool (*decompress)(const std::string &src, int dstSize, std::string &dst);
};

//! The one place the LZO service comes from: sets it and returns the one it
//! replaces; nullptr restores the built-in one (none without Qt).
DVAPI const TLzoService *setLzoService(const TLzoService *service);

class DVAPI TRasterCodecLZO final : public TRasterCodec {
public:
  TRasterCodecLZO(const std::string &name, bool useCache);
  ~TRasterCodecLZO();

  TRasterP compress(const TRasterP &inRas, int allocUnit, TINT32 &outDataSize);
  bool decompress(const UCHAR *inData, TINT32 inDataSize, TRasterP &outRas,
                  bool safeMode);
  void decompress(const TRasterP &compressedRas, TRasterP &outRas);

  void reset() {
    if (m_useCache) return;
    m_raster = TRasterGR8P();
  }

private:
  TRasterGR8P m_raster;
  std::string m_cacheId;
  bool m_useCache;

private:
  UINT doCompress(const TRasterP &inRas, int allocUnit, TRasterGR8P &outRas);
};

#endif
