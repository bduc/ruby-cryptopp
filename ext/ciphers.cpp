
/*
 * Copyright (c) 2002-2014 J Smith <dark.panda@gmail.com>
 * Crypto++ copyright (c) 1995-2013 Wei Dai
 * See MIT-LICENSE for the extact license
 */

#include "j3way.h"
#include "jaes.h"
#include "jblowfish.h"
#include "jcamellia.h"
#include "jcast128.h"
#include "jcast256.h"
#include "jdes.h"
#include "jdes_ede2.h"
#include "jdes_ede3.h"
#include "jdes_xex3.h"
#include "jdiamond2.h"
#include "jdiamond2lite.h"
#include "jgost.h"
#include "jidea.h"
#include "jmars.h"
#include "jrc2.h"
#include "jrc5.h"
#include "jrc6.h"
#include "jsafer.h"
#include "jserpent.h"
#include "jshacal2.h"
#include "jshark.h"
#include "jskipjack.h"
#include "jsquare.h"
#include "jtea.h"
#include "jtwofish.h"


// stream cipher algorithms:

#include "jarc4.h"
#include "jmarc4.h"
#include "jpanamacipher.h"
#include "jseal.h"

#include "jbasiccipherinfo.h"
#include "jexception.h"

#include "cryptopp_ruby_api.h"

extern void cipher_mark(JBase *c);
extern void cipher_free(JBase *c);

// forward declarations

static CipherEnum cipher_sym_to_const(VALUE cipher);
static ModeEnum mode_sym_to_const(VALUE m);
static PaddingEnum padding_sym_to_const(VALUE p);
static RNGEnum rng_sym_to_const(VALUE rng);

static bool cipher_enabled(CipherEnum cipher);
static void cipher_options(VALUE self, VALUE options);
static JBase* cipher_factory(long algorithm);
static VALUE wrap_cipher_in_ruby(JBase* cipher);
static void cipher_rand_iv(VALUE self, VALUE l);
static string cipher_iv_eq(VALUE self, VALUE iv, bool hex);
static string cipher_iv(VALUE self, bool hex);
static string cipher_plaintext_eq(VALUE self, VALUE plaintext, bool hex);
static string cipher_plaintext(VALUE self, bool hex);
static string cipher_ciphertext_eq(VALUE self, VALUE ciphertext, bool hex);
static string cipher_ciphertext(VALUE self, bool hex);
static string cipher_key_eq(VALUE self, VALUE key, bool hex);
static string cipher_key(VALUE self, bool hex);
static VALUE cipher_encrypt(VALUE self, bool hex);
static VALUE cipher_decrypt(VALUE self, bool hex);

static CipherEnum cipher_sym_to_const(VALUE c)
{
  CipherEnum cipher = UNKNOWN_CIPHER;
  ID id = SYM2ID(c);

  if (id == rb_intern("rijndael")) {
    cipher = AES_CIPHER;
  }
  else if (id == rb_intern("panama")) {
    cipher = PANAMA_CIPHER;
  }
  else if (id == rb_intern("seal")) {
    cipher = SEAL_CIPHER;
  }
#  define CIPHER_ALGORITHM_X_FORCE 1
#  define CIPHER_ALGORITHM_X(klass, r, c, s) \
    else if (id == rb_intern(# s)) { \
      cipher = r ## _CIPHER; \
    }
#  include "defs/ciphers.def"
  return cipher;
}


static ModeEnum mode_sym_to_const(VALUE m)
{
  ModeEnum mode = UNKNOWN_MODE;
  ID id = SYM2ID(m);

  if (id == rb_intern("counter")) {
    mode = CTR_MODE;
  }
#  define BLOCK_MODE_X(c, s) \
    else if (id == rb_intern(# s)) { \
      mode = c ## _MODE; \
    }
#  include "defs/block_modes.def"
  return mode;
}

static PaddingEnum padding_sym_to_const(VALUE p)
{
  PaddingEnum padding = UNKNOWN_PADDING;
  ID id = SYM2ID(p);

  if (id == rb_intern("none")) {
    padding = NO_PADDING;
  }
  else if (id == rb_intern("zeroes")) {
    padding = ZEROS_PADDING;
  }
  else if (id == rb_intern("one_and_zeroes")) {
    padding = ONE_AND_ZEROS_PADDING;
  }
#  define PADDING_X(c, s) \
    else if (id == rb_intern(# s)) { \
      padding = c ## _PADDING; \
    }
#  include "defs/paddings.def"
  return padding;
}

static RNGEnum rng_sym_to_const(VALUE r)
{
  RNGEnum rng = UNKNOWN_RNG;
  ID id = SYM2ID(r);

  if (false) {
    // no-op so we can use our x-macro
  }
#  define RNG_X(c, s) \
    else if (id == rb_intern(# s)) { \
      rng = c ## _RNG; \
    }
#  include "defs/rngs.def"
  return rng;
}


/* See if a cipher algorithm is enabled. */
static bool cipher_enabled(CipherEnum cipher)
{
  switch (cipher) {
#    define CIPHER_ALGORITHM_X(klass, r, c, s) \
      case r ##_CIPHER:
#    include "defs/ciphers.def"
      return true;
  }
  return false;
}


/* Figure out options for a cipher. We only check for Symbols, not Strings. */
static void cipher_options(VALUE self, VALUE options)
{
  Check_Type(options, T_HASH);

  {
    VALUE plaintext = rb_hash_aref(options, ID2SYM(rb_intern("plaintext")));
    VALUE plaintext_hex = rb_hash_aref(options, ID2SYM(rb_intern("plaintext_hex")));
    if (!NIL_P(plaintext) && !NIL_P(plaintext_hex)) {
      rb_raise(rb_eCryptoPP_Error, "can't set both plaintext and plaintext_hex in options");
    }
    else if (!NIL_P(plaintext)) {
      cipher_plaintext_eq(self, plaintext, false);
    }
    else if (!NIL_P(plaintext_hex)) {
      cipher_plaintext_eq(self, plaintext_hex, true);
    }
  }

  {
    VALUE ciphertext = rb_hash_aref(options, ID2SYM(rb_intern("ciphertext")));
    VALUE ciphertext_hex = rb_hash_aref(options, ID2SYM(rb_intern("ciphertext_hex")));
    if (!NIL_P(ciphertext) && !NIL_P(ciphertext_hex)) {
      rb_raise(rb_eCryptoPP_Error, "can't set both ciphertext and ciphertext_hex in options");
    }
    else if (!NIL_P(ciphertext)) {
      cipher_ciphertext_eq(self, ciphertext, false);
    }
    else if (!NIL_P(ciphertext_hex)) {
      cipher_ciphertext_eq(self, ciphertext_hex, true);
    }
  }

  {
    VALUE key = rb_hash_aref(options, ID2SYM(rb_intern("key")));
    VALUE key_hex = rb_hash_aref(options, ID2SYM(rb_intern("key_hex")));
    if (!NIL_P(key) && !NIL_P(key_hex)) {
      rb_raise(rb_eCryptoPP_Error, "can't set both key and key_hex in options");
    }
    else if (!NIL_P(key)) {
      cipher_key_eq(self, key, false);
    }
    else if (!NIL_P(key_hex)) {
      cipher_key_eq(self, key_hex, true);
    }
  }

  {
    VALUE key_length = rb_hash_aref(options, ID2SYM(rb_intern("key_length")));
    if (!NIL_P(key_length)) {
      rb_cipher_key_length_eq(self, key_length);
    }
  }

#  if ENABLED_RC2_CIPHER
  {
    VALUE effective_key_length = rb_hash_aref(options, ID2SYM(rb_intern("effective_key_length")));
    if (!NIL_P(effective_key_length)) {
      rb_cipher_effective_key_length_eq(self, effective_key_length);
    }
  }
#  endif

  {
    VALUE rounds = rb_hash_aref(options, ID2SYM(rb_intern("rounds")));
    if (!NIL_P(rounds)) {
      rb_cipher_rounds_eq(self, rounds);
    }
  }

  {
    VALUE rng = rb_hash_aref(options, ID2SYM(rb_intern("rng")));
    if (!NIL_P(rng)) {
      rb_cipher_rng_eq(self, rng);
    }
  }

  {
    VALUE rand_iv = rb_hash_aref(options, ID2SYM(rb_intern("rand_iv")));
    VALUE iv = rb_hash_aref(options, ID2SYM(rb_intern("iv")));
    VALUE iv_hex = rb_hash_aref(options, ID2SYM(rb_intern("iv_hex")));

    if (!NIL_P(rand_iv) && (!NIL_P(iv) || !NIL_P(iv_hex))) {
      rb_raise(rb_eCryptoPP_Error, "can't set both rand_iv and iv or iv_hex in options");
    }
    else if (!NIL_P(iv) && !NIL_P(iv_hex)) {
      rb_raise(rb_eCryptoPP_Error, "can't set both iv and iv_hex in options");
    }
    else if (!NIL_P(rand_iv)) {
      cipher_rand_iv(self, rand_iv);
    }
    else if (!NIL_P(iv)) {
      cipher_iv_eq(self, iv, false);
    }
    else if (!NIL_P(iv_hex)) {
      cipher_iv_eq(self, iv_hex, true);
    }
  }

  {
    VALUE block_mode = rb_hash_aref(options, ID2SYM(rb_intern("block_mode")));
    if (!NIL_P(block_mode)) {
      rb_cipher_block_mode_eq(self, block_mode);
    }
  }

  {
    VALUE padding = rb_hash_aref(options, ID2SYM(rb_intern("padding")));
    if (!NIL_P(padding)) {
      rb_cipher_padding_eq(self, padding);
    }
  }
}


/* Creates a new Crypto++ cipher. May throw a JException if no suitable cipher
 * is found. May throw a JException if no suitable cipher is found. */
static JBase* cipher_factory(VALUE algorithm)
{
  try {
    switch (cipher_sym_to_const(algorithm)) {
      default:
        throw JException("the requested algorithm cannot be found");
      break;

#      define CIPHER_ALGORITHM_X(klass, r, c, s) \
        case r ## _CIPHER: \
          return static_cast<c*>(new c);
#      include "defs/ciphers.def"
    }
  }
  catch (Exception e) {
    throw JException("Crypto++ exception: " + e.GetWhat());
  }
}

/* Wraps a Cipher object into a Ruby object. May throw a JException if no
 * suitable Cipher is found. */
static VALUE wrap_cipher_in_ruby(JBase* cipher)
{
  const type_info& info = typeid(*cipher);
#  define CIPHER_ALGORITHM_X(klass, r, c, s) \
    if (info == typeid(c)) { \
      return Data_Wrap_Struct(rb_cCryptoPP_Cipher_## r, cipher_mark, cipher_free, cipher); \
    } \
    else
#  include "defs/ciphers.def"
  {
    throw JException("the requested algorithm has been disabled");
  }
}

/**
 *  call-seq:
 *    cipher_factory(algorithm)           => Cipher
 *    cipher_factory(algorithm, options)  => Cipher
 *
 * Creates a new Cipher object.
 *
 * See the Cipher class for available options.
 */
VALUE rb_module_cipher_factory(int argc, VALUE *argv, VALUE self)
{
  VALUE algorithm, options, retval;
  rb_scan_args(argc, argv, "11", &algorithm, &options);
  try {
    retval = wrap_cipher_in_ruby(cipher_factory(algorithm));
  }
  catch (Exception& e) {
    rb_raise(rb_eCryptoPP_Error, "%s", e.GetWhat().c_str());
  }
  if (!NIL_P(options)) {
    cipher_options(retval, options);
  }
  return retval;
}

#define CIPHER_ALGORITHM_X(klass, r, n, s) \
VALUE rb_cipher_ ## r ##_new(int argc, VALUE *argv, VALUE self) \
{ \
  VALUE options, retval; \
  rb_scan_args(argc, argv, "01", &options); \
  try { \
    retval = wrap_cipher_in_ruby(cipher_factory(ID2SYM(rb_intern(# s)))); \
  } \
  catch (Exception& e) { \
    rb_raise(rb_eCryptoPP_Error, "%s", e.GetWhat().c_str()); \
  } \
  if (!NIL_P(options)) { \
    cipher_options(retval, options); \
  } \
  return retval; \
}
#include "defs/ciphers.def"


/* Creates a random initialization vector on the cipher. */
static void cipher_rand_iv(VALUE self, VALUE l)
{
  JBase *cipher = NULL;
  unsigned int length = NUM2UINT(rb_funcall(l, rb_intern("to_i"), 0));
  Data_Get_Struct(self, JBase, cipher);
  cipher->setRandIV(length);
}

/**
 * call-seq:
 *    rand_iv(length) => nil
 *
 * Sets a random initialization vector on the Cipher. This method uses the
 * random number generator set on the Cipher to produce the IV, so be sure to
 * set the RNG before you try creating a random IV.
 *
 * The random IV created will is returned in binary.
 */
VALUE rb_cipher_rand_iv(VALUE self, VALUE l)
{
  JBase *cipher = NULL;
  unsigned int length = NUM2UINT(rb_funcall(l, rb_intern("to_i"), 0));
  Data_Get_Struct(self, JBase, cipher);
  cipher->setRandIV(length);
  return l;
}


/* Sets an IV on the cipher. */
static string cipher_iv_eq(VALUE self, VALUE iv, bool hex)
{
  JBase *cipher = NULL;
  Check_Type(iv, T_STRING);
  Data_Get_Struct(self, JBase, cipher);
  cipher->setIV(string(StringValuePtr(iv), RSTRING_LEN(iv)), hex);
  return cipher->getIV(hex);
}

/**
 * call-seq:
 *    iv=(iv) => String
 *
 * Set an initialization vector on the Cipher.
 */
VALUE rb_cipher_iv_eq(VALUE self, VALUE iv)
{
  cipher_iv_eq(self, iv, false);
  return iv;
}

/**
 * call-seq:
 *    iv_hex=(iv) => String
 *
 * Set an initialization vector on the Cipher. This method uses hex data and
 * returns the IV as set.
 */
VALUE rb_cipher_iv_hex_eq(VALUE self, VALUE iv)
{
  cipher_iv_eq(self, iv, true);
  return iv;
}


/* Gets the IV. */
static string cipher_iv(VALUE self, bool hex)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return cipher->getIV(hex);
}

/**
 * call-seq:
 *    iv => String
 *
 * Returns the Cipher's IV in binary.
 */
VALUE rb_cipher_iv(VALUE self)
{
  string retval = cipher_iv(self, false);
  return rb_tainted_str_new(retval.data(), retval.length());
}

/**
 * call-seq:
 *    iv => String
 *
 * Returns the Cipher's IV in hex.
 */
VALUE rb_cipher_iv_hex(VALUE self)
{
  string retval = cipher_iv(self, true);
  return rb_tainted_str_new(retval.data(), retval.length());
}


/**
 * call-seq:
 *    block_mode=(mode) => Symbol
 *
 * Set the block mode on block ciphers. Returns the mode as set and may raise
 * a CryptoPPError if the mode is invalid or you are using a stream cipher.
 */
VALUE rb_cipher_block_mode_eq(VALUE self, VALUE m)
{
  JBase *cipher = NULL;
  ModeEnum mode = mode_sym_to_const(m);

  if (!VALID_MODE(mode)) {
    rb_raise(rb_eCryptoPP_Error, "invalid cipher mode");
  }
  Data_Get_Struct(self, JBase, cipher);
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    rb_raise(rb_eCryptoPP_Error, "can't set mode on stream ciphers");
  }
  else {
    ((JCipher*) cipher)->setMode(mode);
    return m;
  }
}


/**
 * call-seq:
 *    block_mode => Fixnum
 *
 * Get the block mode. Returns <tt>nil</tt> if you try to use this on a stream
 * cipher.
 */
VALUE rb_cipher_block_mode(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  ModeEnum mode = ((JCipher*) cipher)->getMode();
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    return Qnil;
  }
#  define BLOCK_MODE_X(c, s) \
    else if (mode == c ## _MODE) { \
      return ID2SYM(rb_intern(# s)); \
    }
#  include "defs/block_modes.def"
}


/**
 * call-seq:
 *    padding=(padding) => Symbol
 *
 * Set the padding on block ciphers.
 *
 * Note that not all block cipher modes can use all of the padding types.
 * A CryptoPPError will be raised if you try to set an invalid padding. Also
 * note that the padding should be set <i>AFTER</i> the block mode, as setting
 * the mode causes the padding to revert to its default setting.
 *
 * Returns the padding as set.
 */
VALUE rb_cipher_padding_eq(VALUE self, VALUE p)
{
  JBase *cipher = NULL;
  PaddingEnum padding = padding_sym_to_const(p);

  if (!VALID_PADDING(padding)) {
    rb_raise(rb_eCryptoPP_Error, "invalid cipher padding");
  }
  Data_Get_Struct(self, JBase, cipher);
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    rb_raise(rb_eCryptoPP_Error, "can't set padding on stream ciphers");
  }
  else {
    ((JCipher*) cipher)->setPadding(padding);
    if (((JCipher*) cipher)->getPadding() != padding) {
      rb_raise(rb_eCryptoPP_Error, "Padding '%s' cannot be used with mode '%s'", JCipher::getPaddingName(padding).c_str(), ((JCipher*) cipher)->getModeName().c_str());
    }
    else {
      return p;
    }
  }
}


/**
 * call-seq:
 *    padding => Fixnum
 *
 * Get the cipher padding being used. Returns <tt>nil</tt> if you try to use
 * this on a stream cipher.
 */
VALUE rb_cipher_padding(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  PaddingEnum padding = ((JCipher*) cipher)->getPadding();
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    return Qnil;
  }
#  define PADDING_X(c, s) \
    else if (padding == c ## _PADDING) { \
      return ID2SYM(rb_intern(# s)); \
    }
#  include "defs/paddings.def"
}


/**
 * call-seq:
 *    rng=(rng) => Symbol
 *
 * Set the random number generator to use for IVs. A CryptoPPError will be
 * raised if an RNG is not available on the system.
 *
 * RNGs are used to create random initialization vectors using
 * <tt>rand_iv</tt>. The default is a non-blocking RNG, such as
 * <tt>/dev/urandom</tt> on UNIX-alikes or CryptoAPI on Microsoft systems.
 */
VALUE rb_cipher_rng_eq(VALUE self, VALUE r)
{
  JBase *cipher = NULL;
  RNGEnum rng = rng_sym_to_const(r);

  if (!VALID_RNG(rng)) {
    rb_raise(rb_eCryptoPP_Error, "invalid cipher RNG");
  }
  Data_Get_Struct(self, JBase, cipher);
  ((JCipher*) cipher)->setRNG(rng);
  if (((JCipher*) cipher)->getRNG() != rng) {
    rb_raise(rb_eCryptoPP_Error, "RNG '%s' is unavailable", JBase::getRNGName(rng).c_str());
  }
  else {
    return r;
  }
}


/**
 * call-seq:
 *    rng => Fixnum
 *
 * Get the RNG being used.
 */
VALUE rb_cipher_rng(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  RNGEnum rng = ((JCipher*) cipher)->getRNG();
  if (false) {
    // no-op so we can use our x-macro
  }
#  define RNG_X(c, s) \
    else if (rng == c ## _RNG) { \
      return ID2SYM(rb_intern(# s)); \
    }
#  include "defs/rngs.def"
}


/* Set the plaintext. */
static string cipher_plaintext_eq(VALUE self, VALUE plaintext, bool hex)
{
  JBase *cipher = NULL;
  Check_Type(plaintext, T_STRING);
  Data_Get_Struct(self, JBase, cipher);
  cipher->setPlaintext(string(StringValuePtr(plaintext), RSTRING_LEN(plaintext)), hex);
  return cipher->getPlaintext(hex);
}

/**
 * call-seq:
 *    plaintext=(string) => String
 *
 * Sets the plaintext on the Cipher in binary and returns the same.
 */
VALUE rb_cipher_plaintext_eq(VALUE self, VALUE plaintext)
{
  cipher_plaintext_eq(self, plaintext, false);
  return plaintext;
}

/**
 * call-seq:
 *    plaintext_hex=(string) => String
 *
 * Sets the plaintext on the Cipher in hex and returns the same.
 */
VALUE rb_cipher_plaintext_hex_eq(VALUE self, VALUE plaintext)
{
  cipher_plaintext_eq(self, plaintext, true);
  return plaintext;
}


/* Get the plaintext. */
static string cipher_plaintext(VALUE self, bool hex)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return cipher->getPlaintext(hex);
}

/**
 * call-seq:
 *    plaintext => String
 *
 * Gets the plaintext from the Cipher in binary.
 */
VALUE rb_cipher_plaintext(VALUE self)
{
  string retval = cipher_plaintext(self, false);
  return rb_tainted_str_new(retval.data(), retval.length());
}

/**
 * call-seq:
 *    plaintext_hex => String
 *
 * Gets the plaintext from the Cipher in hex.
 */
VALUE rb_cipher_plaintext_hex(VALUE self)
{
  string retval = cipher_plaintext(self, true);
  return rb_tainted_str_new(retval.data(), retval.length());
}


/* Set the ciphertext. */
static string cipher_ciphertext_eq(VALUE self, VALUE ciphertext, bool hex)
{
  JBase *cipher = NULL;
  Check_Type(ciphertext, T_STRING);
  Data_Get_Struct(self, JBase, cipher);
  cipher->setCiphertext(string(StringValuePtr(ciphertext), RSTRING_LEN(ciphertext)), hex);
  return cipher->getCiphertext(hex);
}

/**
 * call-seq:
 *    ciphertext=(string) => String
 *
 * Sets the ciphertext on the Cipher in binary and returns the same.
 */
VALUE rb_cipher_ciphertext_eq(VALUE self, VALUE ciphertext)
{
  cipher_ciphertext_eq(self, ciphertext, false);
  return ciphertext;
}

/**
 * call-seq:
 *    ciphertext_hex=(string) => String
 *
 * Sets the ciphertext on the Cipher in hex and returns the same.
 */
VALUE rb_cipher_ciphertext_hex_eq(VALUE self, VALUE ciphertext)
{
  cipher_ciphertext_eq(self, ciphertext, true);
  return ciphertext;
}


/* Get the ciphertext. */
static string cipher_ciphertext(VALUE self, bool hex)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return cipher->getCiphertext(hex);
}

/**
 * call-seq:
 *    ciphertext => String
 *
 * Gets the ciphertext from the Cipher in binary.
 */
VALUE rb_cipher_ciphertext(VALUE self)
{
  string retval = cipher_ciphertext(self, false);
  return rb_tainted_str_new(retval.data(), retval.length());
}

/**
 * call-seq:
 *    ciphertext_hex => String
 *
 * Gets the ciphertext from the Cipher in hex.
 */
VALUE rb_cipher_ciphertext_hex(VALUE self)
{
  string retval = cipher_ciphertext(self, true);
  return rb_tainted_str_new(retval.data(), retval.length());
}


/* Set the key. The true length of the key might not be what you expect,
 * as different algorithms behave differently, i.e. 3Way has a fixed keylength
 * of 12 bytes, while Blowfish can use keys of 1 to 72 bytes. */
static string cipher_key_eq(VALUE self, VALUE key, bool hex)
{
  JBase *cipher = NULL;
  Check_Type(key, T_STRING);
  Data_Get_Struct(self, JBase, cipher);
  cipher->setKey(string(StringValuePtr(key), RSTRING_LEN(key)), hex);
  return cipher->getKey(hex);
}

/**
 * call-seq:
 *    key=(string) => String
 *
 * Sets the key on the Cipher in binary and returns the same. Note that the
 * key try to set may be truncated or padded depending on its length. Some
 * ciphers have fixed key lengths while others have specific requirements on
 * their size. For instance, the Threeway cipher has a fixed key length of 12
 * bytes, while Blowfish can use keys of 1 to 72 bytes.
 *
 * When the key being used is shorter than an allowed key length, the key
 * will be padded with \0's before being set. When the key is longer than
 * an allowed key length, the key will be truncated before being set.
 *
 * In both cases, this happens automatically. You should check the actual
 * key that is set using <tt>#key</tt>.
 */
VALUE rb_cipher_key_eq(VALUE self, VALUE key)
{
  cipher_key_eq(self, key, false);
  return key;
}

/**
 * call-seq:
 *    key_hex=(string) => String
 *
 * Sets the key on the Cipher in hex and returns the same. Note that the
 * key try to set may be truncated or padded depending on its length. Some
 * ciphers have fixed key lengths while others have specific requirements on
 * their size. For instance, the Threeway cipher has a fixed key length of 12
 * bytes, while Blowfish can use keys of 1 to 72 bytes.
 *
 * When the key being used is shorter than an allowed key length, the key
 * will be padded with \0's before being set. When the key is longer than
 * an allowed key length, the key will be truncated before being set.
 *
 * In both cases, this happens automatically. You should check the actual
 * key that is set using <tt>#key</tt>.
 */
VALUE rb_cipher_key_hex_eq(VALUE self, VALUE key)
{
  cipher_key_eq(self, key, true);
  return key;
}


/* Get the key. */
static string cipher_key(VALUE self, bool hex)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return cipher->getKey(hex);
}

/**
 * call-seq:
 *    key => String
 *
 * Returns the key set on the Cipher in binary.
 */
VALUE rb_cipher_key(VALUE self)
{
  string retval = cipher_key(self, false);
  return rb_tainted_str_new(retval.data(), retval.length());
}

/**
 * call-seq:
 *    key => String
 *
 * Returns the key set on the Cipher in hex.
 */
VALUE rb_cipher_key_hex(VALUE self)
{
  string retval = cipher_key(self, true);
  return rb_tainted_str_new(retval.data(), retval.length());
}


/**
 * call-seq:
 *    key_length=(length) => Fixnum
 *
 * Sets the key length. Some ciphers require rather specific key lengths,
 * and if the key length you attempt to set is invalid, an exception will
 * be thrown. The key length being set is set in terms of bytes in binary, not
 * hex characters.
 */
VALUE rb_cipher_key_length_eq(VALUE self, VALUE l)
{
  JBase *cipher = NULL;
  unsigned int length = NUM2UINT(rb_funcall(l, rb_intern("to_i"), 0));
  Data_Get_Struct(self, JBase, cipher);
  cipher->setKeylength(length);
  if (cipher->getKeylength() != length) {
    rb_raise(rb_eCryptoPP_Error, "tried to set a key length of %d but %d was used", length, cipher->getKeylength());
  }
  else {
    return l;
  }
}


/**
 * call-seq:
 *    key_length => Fixnum
 *
 * Gets the key length. The key length being returned in terms of bytes
 * in binary, not hex characters.
 */
VALUE rb_cipher_key_length(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getKeylength());
}


/**
 * Returns the default key length.
 */
VALUE rb_cipher_default_key_length(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getDefaultKeylength());
}

/**
 * Returns the minimum key length.
 */
VALUE rb_cipher_min_key_length(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getMinKeylength());
}

/**
 * Returns the maximum key length.
 */
VALUE rb_cipher_max_key_length(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getMaxKeylength());
}

/**
 * Returns the multiplier used for the key length.
 */
VALUE rb_cipher_mult_key_length(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getMultKeylength());
}

/**
 * Returns the closest valid key length without actually setting it.
 */
VALUE rb_cipher_valid_key_length(VALUE self, VALUE l)
{
  JBase *cipher = NULL;
  unsigned int length = NUM2UINT(l);
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getValidKeylength(length));
}


#if ENABLED_RC2_CIPHER
/**
 * call-seq:
 *    effect_key_length=(length) => Fixnum
 *
 * Set the effective keylength on the RC2 algorithm. This function can
 * only be used with RC2. Returns the actual effective keylength set. The
 * default is 1024, which is also the maximum.
 */
VALUE rb_cipher_effective_key_length_eq(VALUE self, VALUE l)
{
  JBase *cipher = NULL;
  unsigned int length = NUM2UINT(rb_funcall(l, rb_intern("to_i"), 0));
  Data_Get_Struct(self, JBase, cipher);
  if (cipher->getCipherType() != RC2_CIPHER) {
    rb_raise(rb_eCryptoPP_Error, "effective key lengths can only be used with the RC2 cipher");
  }
  else {
    ((JRC2*) cipher)->setEffectiveKeylength(length);
    if (((JRC2*) cipher)->getEffectiveKeylength() != length) {
      rb_raise(rb_eCryptoPP_Error, "tried to set an effective key length of %d but %d was used", length, ((JRC2*) cipher)->getEffectiveKeylength());
    }
    else {
      return l;
    }
  }
}


/**
 * Gets the key length. The key length being returned in terms of bytes in
 * binary, not hex characters.
 */
VALUE rb_cipher_effective_key_length(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(((JRC2*) cipher)->getEffectiveKeylength());
}
#endif


/**
 * call-seq:
 *     block_size => Fixnum
 *
 * Gets the block size.
 */
VALUE rb_cipher_block_size(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_fix_new(cipher->getBlockSize());
}


/**
 * call-seq:
 *    rounds=(rounds) => Fixnum
 *
 * Sets the number of rounds to perform on block ciphers. Some block ciphers
 * have different requirements for their rounds than others. An exception
 * will be raised on invalid round settings.
 */
VALUE rb_cipher_rounds_eq(VALUE self, VALUE r)
{
  JBase *cipher = NULL;
  unsigned int rounds = NUM2UINT(rb_funcall(r, rb_intern("to_i"), 0));
  Data_Get_Struct(self, JBase, cipher);
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    rb_raise(rb_eCryptoPP_Error, "can't set rounds on stream ciphers");
  }
  else {
    ((JCipher*) cipher)->setRounds(rounds);
    if (((JCipher*) cipher)->getRounds() != rounds) {
      rb_raise(rb_eCryptoPP_Error, "tried set the number of rounds to %d but %d was used instead", rounds, ((JCipher*) cipher)->getRounds());
    }
    else {
      return r;
    }
  }
}


/**
 * call-seq:
 *     rounts => Fixnum
 *
 * Gets the number of rounds to perform on block ciphers. Returns nil if you
 * try to use this on a stream cipher.
 */
VALUE rb_cipher_rounds(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    return Qnil;
  }
  else {
    return rb_fix_new(((JCipher*) cipher)->getRounds());
  }
}


/* Encrypt the plaintext using the options set on the Cipher. This method will
 * return the ciphertext in binary or hex accordingly, but the raw ciphertext
 * will always be available through the ciphertext methods regardless. */
static VALUE cipher_encrypt(VALUE self, bool hex)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  try {
    cipher->encrypt();
    return rb_tainted_str_new(cipher->getCiphertext(hex).data(), cipher->getCiphertext(hex).length());
  }
  catch (Exception e) {
    rb_raise(rb_eCryptoPP_Error, "Crypto++ exception: %s", e.GetWhat().c_str());
  }
}

/**
 * call-seq:
 *     encrypt => String
 *
 * Encrypt the plaintext using the options set on the Cipher. This method will
 * return the ciphertext in binary. The raw ciphertext will always be available
 * through the ciphertext and ciphertext_hex afterwards.
 */
VALUE rb_cipher_encrypt(VALUE self)
{
  return cipher_encrypt(self, false);
}

/**
 * call-seq:
 *     encrypt_hex => String
 *
 * Encrypt the plaintext using the options set on the Cipher. This method will
 * return the ciphertext in hex. The raw ciphertext will always be available
 * through the ciphertext and ciphertext_hex afterwards.
 */
VALUE rb_cipher_encrypt_hex(VALUE self)
{
  return cipher_encrypt(self, true);
}


/* Decrypt the ciphertext using the options set on the Cipher and store
 * it in the plaintext attribute. This method will return the plaintext
 * in binary or hex accordingly, but the raw plaintext will always be
 * available through the plaintext methods regardless. */
static VALUE cipher_decrypt(VALUE self, bool hex)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  try {
    cipher->decrypt();
    string retval = cipher->getPlaintext(hex);
    return rb_tainted_str_new(retval.data(), retval.length());
  }
  catch (Exception e) {
    rb_raise(rb_eCryptoPP_Error, "Crypto++ exception: %s", e.GetWhat().c_str());
  }
}

/**
 * call-seq:
 *     decrypt => String
 *
 * Decrypt the ciphertext using the options set on the Cipher. This method
 * will return the plaintext in binary. The raw plaintext will always be
 * available through the plaintext and plaintext_hex methods afterwards.
 */
VALUE rb_cipher_decrypt(VALUE self)
{
  return cipher_decrypt(self, false);
}

/**
 * call-seq:
 *     decrypt_hex => String
 *
 * Decrypt the ciphertext using the options set on the Cipher. This method
 * will return the plaintext in hex. The raw plaintext will always be
 * available through the plaintext and plaintext_hex methods afterwards.
 */
VALUE rb_cipher_decrypt_hex(VALUE self)
{
  return cipher_decrypt(self, true);
}


/**
 * call-seq:
 *    encrypt_io(in, out) => true
 *
 * Encrypts a Ruby IO object and spits the result into another one. You can use
 * any sort of Ruby object as long as it implements <tt>eof?</tt>,
 * <tt>read</tt>, <tt>write</tt> and <tt>flush</tt>.
 *
 * Examples:
 *
 *  cipher.encrypt_io(File.open("http://example.com/"), File.open("test.out", 'w'))
 *
 *  output = StringIO.new
 *  cipher.encrypt_io(File.open('test.enc'), output)
 */
VALUE rb_cipher_encrypt_io(VALUE self, VALUE in, VALUE out)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  try {
    cipher->encryptRubyIO(&in, &out);
    return Qtrue;
  }
  catch (Exception e) {
    rb_raise(rb_eCryptoPP_Error, "Crypto++ exception: %s", e.GetWhat().c_str());
  }
}


/**
 * call-seq:
 *    decrypt_io(in, out) => true
 *
 * Decrypts a Ruby IO object and spits the result into another one. You can use
 * any sort of Ruby object as long as it implements <tt>eof?</tt>,
 * <tt>read</tt>, <tt>write</tt> and <tt>flush</tt>.
 *
 * Examples:
 *
 *  cipher.decrypt_io(File.open("http://example.com/"), File.open("test.out", 'w'))
 *
 *  output = StringIO.new
 *  cipher.decrypt_io(File.open('test.enc'), output)
 */
VALUE rb_cipher_decrypt_io(VALUE self, VALUE in, VALUE out)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  try {
    cipher->decryptRubyIO(&in, &out);
    return Qtrue;
  }
  catch (Exception e) {
    rb_raise(rb_eCryptoPP_Error, "Crypto++ exception: %s", e.GetWhat().c_str());
  }
}


/**
 * call-seq:
 *    cipher_name(algorithm) => String
 *
 * Returns the name of a Cipher.
 */
VALUE rb_module_cipher_name(VALUE self, VALUE c)
{
  switch (cipher_sym_to_const(c)) {
    default:
      rb_raise(rb_eCryptoPP_Error, "could not find a valid cipher type");
    break;

#    define CIPHER_ALGORITHM_X(klass, r, c, s) \
      case r ## _CIPHER: \
        return rb_tainted_str_new2(c::getStaticCipherName().c_str());
#    include "defs/ciphers.def"
  }
}


/**
 * call-seq:
 *    algorithm_name() => String
 *
 * Returns the name of the cipher algorithm being used.
 */
VALUE rb_cipher_algorithm_name(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_tainted_str_new2(cipher->getCipherName().c_str());
}


/**
 * call-seq:
 *    block_mode_name(block_mode) => String
 *
 * Singleton method to return the name of a block cipher mode.
 */
VALUE rb_module_block_mode_name(VALUE self, VALUE m)
{
  return rb_tainted_str_new2(JCipher::getModeName(mode_sym_to_const(m)).c_str());
}


/**
 * call-seq:
 *    block_mode_name() => String
 *
 * Returns the name of the mode being used.
 */
VALUE rb_cipher_block_mode_name(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    return Qnil;
  }
  else {
    return rb_tainted_str_new2(JCipher::getModeName(((JCipher*) cipher)->getMode()).c_str());
  }
}


/**
 * call-seq:
 *    padding_name(padding) => String
 *
 * Singleton method to return the name of the a block cipher padding mode.
 */
VALUE rb_module_padding_name(VALUE self, VALUE p)
{
  return rb_tainted_str_new2(JCipher::getPaddingName(padding_sym_to_const(p)).c_str());
}


/**
 * call-seq:
 *    padding_name() => String
 *
 * Returns the name of the padding being used.
 */
VALUE rb_cipher_padding_name(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  if (IS_STREAM_CIPHER(cipher->getCipherType())) {
    return Qnil;
  }
  else {
    return rb_tainted_str_new2(JCipher::getPaddingName(((JCipher*) cipher)->getPadding()).c_str());
  }
}


/**
 * call-seq:
 *    rng_name(rng) => String
 *
 * Singleton method to return the name of the random number generator being
 * used.
 */
VALUE rb_module_rng_name(VALUE self, VALUE r)
{
  return rb_tainted_str_new2(JCipher::getRNGName(rng_sym_to_const(r)).c_str());
}


/**
 * call-seq:
 *    rng_name() => String
 *
 * Returns the name of the random number generator being used.
 */
VALUE rb_cipher_rng_name(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);
  return rb_tainted_str_new2(JCipher::getRNGName(cipher->getRNG()).c_str());
}


/**
 * call-seq:
 *    cipher_type() => Symbol
 *
 * Returns the type of cipher being used as a Symbol.
 */
VALUE rb_cipher_cipher_type(VALUE self)
{
  JBase *cipher = NULL;
  Data_Get_Struct(self, JBase, cipher);

  switch (cipher->getCipherType()) {
#    define CIPHER_ALGORITHM_X(klass, r, c, s) \
      case r ## _CIPHER: \
        return ID2SYM(rb_intern(# s));
#    include "defs/ciphers.def"

    default:
      return Qnil;
  }
}


/**
 * call-seq:
 *    cipher_enabled?(algorithm) => boolean
 *
 * Singleton method to check for the availability of a cipher algorithm.
 */
VALUE rb_module_cipher_enabled(VALUE self, VALUE c)
{
  switch (cipher_sym_to_const(c)) {
    default:
      return Qfalse;

#  define CIPHER_ALGORITHM_X(klass, r, c, s) \
    case r ## _CIPHER:
#  include "defs/ciphers.def"
      return Qtrue;
  }
}


/**
 * call-seq:
 *    rng_available?(rng) => boolean
 *
 * Singleton method to check for the availability of a random number generator.
 */
VALUE rb_module_rng_available(VALUE self, VALUE r)
{
  ID id = SYM2ID(r);
  if (id == rb_intern("rand")) {
    return Qtrue;
  }
#  ifdef NONBLOCKING_RNG_AVAILABLE
    else if (id == rb_intern("non_blocking")) {
      return Qtrue;
    }
#  endif

#  ifdef BLOCKING_RNG_AVAILABLE
    else if (id == rb_intern("blocking")) {
      return Qtrue;
    }
#  endif

  else {
    return Qfalse;
  }
}


/**
 * call-seq:
 *    cipher_list() => Array
 *
 * Returns an Array of available ciphers.
 */
VALUE rb_module_cipher_list(VALUE self)
{
  VALUE ary = rb_ary_new();

#  define CIPHER_ALGORITHM_X(klass, r, c, s) \
    rb_ary_push(ary, ID2SYM(rb_intern(# s)));
#  include "defs/ciphers.def"

  return ary;
}
