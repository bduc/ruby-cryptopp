
/*
 * Copyright (c) 2002-2014 J Smith <dark.panda@gmail.com>
 * Crypto++ copyright (c) 1995-2013 Wei Dai
 * See MIT-LICENSE for the extact license
 */

#include "jtwofish.h"

#if ENABLED_TWOFISH_CIPHER

BlockCipher* JTwofish::getEncryptionObject()
{
  return new TwofishEncryption((byte*) itsKey.data(), itsKeylength);
}

BlockCipher* JTwofish::getDecryptionObject()
{
  return new TwofishDecryption((byte*) itsKey.data(), itsKeylength);
}

#endif
