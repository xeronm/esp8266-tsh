/* 
 * ESP8266 RFC4634 adapted implementation.
 * Copyright (c) 2018 Denis Muratov <xeronm@gmail.com>.
 * https://dtec.pro/gitbucket/git/esp8266/esp8266-tsh.git
 *
 * This file is part of ESP8266 Things Shell.
 *
 * ESP8266 Things Shell is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/* 
 *  Description: This file implements a unified interface to the SHA algorithms. RFC4634 adapted implementation.
*/

#include "crypto/sha.h"

/*
 *  USHAReset
 *
 *  Description:
 *      This function will initialize the SHA Context in preparation
 *      for computing a new SHA message digest.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to reset.
 *      whichSha: [in]
 *          Selects which SHA reset to call
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int ICACHE_FLASH_ATTR
USHAReset (USHAContext * ctx, enum SHAversion whichSha)
{
    if (ctx) {
	ctx->whichSha = whichSha;
	switch (whichSha) {
	case SHA1:
	    return SHA1Reset ((SHA1Context *) & ctx->ctx);
	case SHA224:
	    return SHA224Reset ((SHA224Context *) & ctx->ctx);
	case SHA256:
	    return SHA256Reset ((SHA256Context *) & ctx->ctx);
	default:
	    return shaBadParam;
	}
    }
    else {
	return shaNull;
    }
}

/*
 *  USHAInput
 *
 *  Description:
 *      This function accepts an array of octets as the next portion
 *      of the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of
 *          the message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int ICACHE_FLASH_ATTR
USHAInput (USHAContext * ctx, const uint8_t * bytes, unsigned int bytecount)
{
    if (ctx) {
	switch (ctx->whichSha) {
	case SHA1:
	    return SHA1Input ((SHA1Context *) & ctx->ctx, bytes, bytecount);
	case SHA224:
	    return SHA224Input ((SHA224Context *) & ctx->ctx, bytes, bytecount);
	case SHA256:
	    return SHA256Input ((SHA256Context *) & ctx->ctx, bytes, bytecount);
	default:
	    return shaBadParam;
	}
    }
    else {
	return shaNull;
    }
}

/*
 * USHAFinalBits
 *
 * Description:
 *   This function will add in any final bits of the message.
 *
 * Parameters:
 *   context: [in/out]
 *     The SHA context to update
 *   message_bits: [in]
 *     The final bits of the message, in the upper portion of the
 *     byte. (Use 0b###00000 instead of 0b00000### to input the
 *     three bits ###.)
 *   length: [in]
 *     The number of bits in message_bits, between 1 and 7.
 *
 * Returns:
 *   sha Error Code.
 */
int ICACHE_FLASH_ATTR
USHAFinalBits (USHAContext * ctx, const uint8_t bits, unsigned int bitcount)
{
    if (ctx) {
	switch (ctx->whichSha) {
	case SHA1:
	    return SHA1FinalBits ((SHA1Context *) & ctx->ctx, bits, bitcount);
	case SHA224:
	    return SHA224FinalBits ((SHA224Context *) & ctx->ctx, bits, bitcount);
	case SHA256:
	    return SHA256FinalBits ((SHA256Context *) & ctx->ctx, bits, bitcount);
	default:
	    return shaBadParam;
	}
    }
    else {
	return shaNull;
    }
}

/*
 * USHAResult
 *
 * Description:
 *   This function will return the 160-bit message digest into the
 *   Message_Digest array provided by the caller.
 *   NOTE: The first octet of hash is stored in the 0th element,
 *      the last octet of hash in the 19th element.
 *
 * Parameters:
 *   context: [in/out]
 *     The context to use to calculate the SHA-1 hash.
 *   Message_Digest: [out]
 *     Where the digest is returned.
 *
 * Returns:
 *   sha Error Code.
 *
 */
int ICACHE_FLASH_ATTR
USHAResult (USHAContext * ctx, uint8_t Message_Digest[USHAMaxHashSize])
{
    if (ctx) {
	switch (ctx->whichSha) {
	case SHA1:
	    return SHA1Result ((SHA1Context *) & ctx->ctx, Message_Digest);
	case SHA224:
	    return SHA224Result ((SHA224Context *) & ctx->ctx, Message_Digest);
	case SHA256:
	    return SHA256Result ((SHA256Context *) & ctx->ctx, Message_Digest);
	default:
	    return shaBadParam;
	}
    }
    else {
	return shaNull;
    }
}

/*
 * USHABlockSize
 *
 * Description:
 *   This function will return the blocksize for the given SHA
 *   algorithm.
 *
 * Parameters:
 *   whichSha:
 *     which SHA algorithm to query
 *
 * Returns:
 *   block size
 *
 */
int ICACHE_FLASH_ATTR
USHABlockSize (enum SHAversion whichSha)
{
    switch (whichSha) {
    case SHA1:
	return SHA1_Message_Block_Size;
    case SHA224:
	return SHA224_Message_Block_Size;
    default:
    case SHA256:
	return SHA256_Message_Block_Size;
    }
}

/*
 * USHAHashSize
 *
 * Description:
 *   This function will return the hashsize for the given SHA
 *   algorithm.
 *
 * Parameters:
 *   whichSha:
 *     which SHA algorithm to query
 *
 * Returns:
 *   hash size
 *
 */
int ICACHE_FLASH_ATTR
USHAHashSize (enum SHAversion whichSha)
{
    switch (whichSha) {
    case SHA1:
	return SHA1HashSize;
    case SHA224:
	return SHA224HashSize;
    default:
    case SHA256:
	return SHA256HashSize;
    }
}

/*
 * USHAHashSizeBits
 *
 * Description:
 *   This function will return the hashsize for the given SHA
 *   algorithm, expressed in bits.
 *
 * Parameters:
 *   whichSha:
 *     which SHA algorithm to query
 *
 * Returns:
 *   hash size in bits
 *
 */
int ICACHE_FLASH_ATTR
USHAHashSizeBits (enum SHAversion whichSha)
{
    switch (whichSha) {
    case SHA1:
	return SHA1HashSizeBits;
    case SHA224:
	return SHA224HashSizeBits;
    default:
    case SHA256:
	return SHA256HashSizeBits;
    }
}
