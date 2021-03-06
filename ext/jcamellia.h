
/*
 * Copyright (c) 2002-2014 J Smith <dark.panda@gmail.com>
 * Crypto++ copyright (c) 1995-2013 Wei Dai
 * See MIT-LICENSE for the extact license
 */

#ifndef __JCAMELLIA_H__
#define __JCAMELLIA_H__

#include "jconfig.h"

#if ENABLED_CAMELLIA_CIPHER

#include "jcipher_t.h"

// Crypto++ headers...

#include "camellia.h"

using namespace CryptoPP;

class JCamellia : public JCipher_Template<Camellia_Info, CAMELLIA_CIPHER>
{
  protected:
    BlockCipher* getEncryptionObject();
    BlockCipher* getDecryptionObject();
};

#endif
#endif
