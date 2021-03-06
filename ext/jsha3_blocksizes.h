// sha3.h - written and placed in the public domain by Wei Dai
//
// This is a patched version of sha3.h that includes definitions for the
// BLOCKSIZE constants that appear to be missing from the header file.
// See http://sourceforge.net/apps/trac/cryptopp/ticket/31 for details.

#ifndef CRYPTOPP_JSHA3_BLOCKSIZES_H
#define CRYPTOPP_JSHA3_BLOCKSIZES_H

#include "cryptlib.h"
#include "secblock.h"

NAMESPACE_BEGIN(CryptoPP)

/// <a href="http://en.wikipedia.org/wiki/SHA-3">SHA-3</a>
class SHA3 : public HashTransformation
{
public:
  SHA3(unsigned int digestSize) : m_digestSize(digestSize) {Restart();}
  unsigned int DigestSize() const {return m_digestSize;}
  std::string AlgorithmName() const {return "SHA-3-" + IntToString(m_digestSize*8);}
  unsigned int OptimalDataAlignment() const {return GetAlignmentOf<word64>();}

  void Update(const byte *input, size_t length);
  void Restart();
  void TruncatedFinal(byte *hash, size_t size);

protected:
  inline unsigned int r() const {return 200 - 2 * m_digestSize;}

  FixedSizeSecBlock<word64, 25> m_state;
  unsigned int m_digestSize, m_counter;
};

class SHA3_224 : public SHA3
{
public:
  CRYPTOPP_CONSTANT(DIGESTSIZE = 28)
  CRYPTOPP_CONSTANT(BLOCKSIZE = 144)
  SHA3_224() : SHA3(DIGESTSIZE) {}
  static const char * StaticAlgorithmName() {return "SHA-3-224";}
};

class SHA3_256 : public SHA3
{
public:
  CRYPTOPP_CONSTANT(DIGESTSIZE = 32)
  CRYPTOPP_CONSTANT(BLOCKSIZE = 136)
  SHA3_256() : SHA3(DIGESTSIZE) {}
  static const char * StaticAlgorithmName() {return "SHA-3-256";}
};

class SHA3_384 : public SHA3
{
public:
  CRYPTOPP_CONSTANT(DIGESTSIZE = 48)
  CRYPTOPP_CONSTANT(BLOCKSIZE = 104)
  SHA3_384() : SHA3(DIGESTSIZE) {}
  static const char * StaticAlgorithmName() {return "SHA-3-384";}
};

class SHA3_512 : public SHA3
{
public:
  CRYPTOPP_CONSTANT(DIGESTSIZE = 64)
  CRYPTOPP_CONSTANT(BLOCKSIZE = 72)
  SHA3_512() : SHA3(DIGESTSIZE) {}
  static const char * StaticAlgorithmName() {return "SHA-3-512";}
};

NAMESPACE_END

#endif
