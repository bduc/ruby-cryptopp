
/*
 * Copyright (c) 2002-2014 J Smith <dark.panda@gmail.com>
 * Crypto++ copyright (c) 1995-2013 Wei Dai
 * See MIT-LICENSE for the extact license
 */

#include "jdes_ede3.h"

#if ENABLED_DES_EDE3_CIPHER

BlockCipher* JDES_EDE3::getEncryptionObject()
{
  return new DES_EDE3_Encryption((byte*) itsKey.data(), itsKeylength);
}

BlockCipher* JDES_EDE3::getDecryptionObject()
{
  return new DES_EDE3_Decryption((byte*) itsKey.data(), itsKeylength);
}

#endif
