/*
 * peach.c  FPGA-Tough CPU Mining Algo
 *
 * Copyright (c) 2019 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.PDF   **** NO WARRANTY ****
 *
 * Date: 05 June 2019
 * Revision: 1
 *
 * This file is subject to the license as found in LICENSE.PDF
 *
 */

#include "peach.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>

/* Prototypes from trigg.o dependency */
byte *trigg_gen(byte *in);
void trigg_expand2(byte *in, byte *out);

void generate_tile(byte** out, uint32_t index, byte* seed, byte * map,  byte * cache);

/*
 * Return 0 if solved, else 1.
 * Note: We can probably just use trigg_eval here.
 */
int peach_eval(byte *bp, byte d)
{
   byte x, i, j, n;

   x = i = j = n = 0;

   for (i = 0; i < HASHLEN; i++) {
      x = *(bp + i);
      if (x != 0) {
         for(j = 7; j > 0; j--) {
            x >>= 1;
            if(x == 0) {
               n += j;
               break;
            }
         }
      break;
      }
      n += 8;
      continue;
   }
   if(n >= d) return 0;
   return 1;
}

uint32_t next_index(uint32_t current_index, byte* current_tile, byte* nonce)
{
	SHA256_CTX ictx;
	uint32_t index;
	byte hash[HASHLEN];

	sha256_init(&ictx);
	sha256_update(&ictx, nonce, HASHLEN);//hash nonce first because we dont want to allow caching of index computation
	sha256_update(&ictx, (byte*) &current_index, 8);
	sha256_update(&ictx, current_tile, TILE);

	sha256_final(&ictx, hash);

	index =  *(uint32_t*)&hash[0]; //read first 4 bytes as unsigned int
	index += *(uint32_t*)&hash[4]; //read next 4 bytes as unsigned int

	return index % MAP;
}

void get_tile(byte** out, uint32_t index, byte* seed, byte * map,  byte * cache)
{
	if(cache[index])
	{
		*out = map + index * TILE;
		return;
	}

	generate_tile(out, index, seed, map, cache);

	cache[index] = 1;
}

void generate_tile(byte** out, uint32_t index, byte* seed, byte * map,  byte * cache)
{
  /**
   * Declarations */
  SHA256_CTX ictx;
  uint32_t op, i1, i2, i3, i4;
  byte bits, _104, _72, *b, *mapp;
  int i, j, k, t, z, exp, offset;
  float *floatp;

  /* set map pointer */
  mapp = &map[index * TILE];
  
  /* begin tile data */
  sha256_init(&ictx);
  sha256_update(&ictx, seed, HASHLEN); //hash seed first because we don't want to allow caching of index computation
  sha256_update(&ictx, (byte*)&index, sizeof(uint32_t));
  sha256_final(&ictx, mapp);
  
  /* set operation variables */
  i1 = *(word32*)&mapp[0] % HASHLEN;
  i2 = *(word32*)&mapp[4] % HASHLEN;
  i3 = *(word32*)&mapp[8] % HASHLEN;
  i4 = *(word32*)&mapp[12] % HASHLEN;
  _104 = 104;
  _72 = 72;

    for(i = j = k = 0; i < TILE; i+=HASHLEN) {
      for(op = 0; j < i+HASHLEN; j+=4) {
        /* set float pointer */
        floatp = (float*)&mapp[j];
        
        /**
         * Order of operations dependant on initial 8 bits
         * Operations:
         *   1) right shift by 4 to obtain the exponent value
         *   2) 50% chance of exponent being negative
         *   3) 50% chance of changing sign of float */
        if(mapp[k] & 1) {
          k++;
          exp = mapp[k++] >> 4;
          if(mapp[k++] & 1) exp ^= 0x80000000;
          if(mapp[k++] & 1) *floatp = -(*floatp);
        } else
        if(mapp[k] & 2) {
          k++;
          exp = mapp[k++] >> 4;
          if(mapp[k++] & 1) *floatp = -(*floatp);
          if(mapp[k++] & 1) exp ^= 0x80000000;
        } else {
          k++;
          if(mapp[k++] & 1) *floatp = -(*floatp);
          exp = mapp[k++] >> 4;
          if(mapp[k++] & 1) exp ^= 0x80000000;
        }

        /* replace NaN's with tileNum */
        if(isnan(*floatp))
          *floatp = (float) index;

        /* perform floating point operation */
        *floatp = ldexpf(*floatp, exp);
        
        /* pre-scramble op */
        op ^= (uint32_t)mapp[j];
      }
      
      /* hash the result of the previous tile's row to the next */
      if(j < TILE) {
        sha256_init(&ictx);
        sha256_update(&ictx, &mapp[i], HASHLEN);
        sha256_update(&ictx, (byte*)&index, sizeof(uint32_t));
        sha256_final(&ictx, &mapp[j]);
      }
      
      /* perform 8x bit manipulations per row */
      for(t = 0; t < 8; t++) {
        /* determine tile byte offset and operation to use */
        offset = i;
        op += (uint32_t)mapp[i + (t * 4)];
        switch(op & 7) {
		  case 0: /* Swap the first and last bit of a byte. */
		  {
            b = mapp + offset + ( (i1 % 2 == 0) ? i1 : i2 );

            *b ^= 0x81;
          }
            break;
          case 1: /* Swap the first and last byte. */
          {
            bits = mapp[offset];
            mapp[offset] = mapp[offset + 31];
            mapp[offset + 31] = bits;
          }
            break;
          case 2: /* XOR two bytes */
          {
            mapp[offset + i4] = mapp[offset + i4] ^ mapp[offset + i3];
          }
            break;
          case 3: /* Alternate +1 and -1 on all bytes */
          {
            for(z = 0; z < 32; z++)
              mapp[offset + z] += (z & 1 == 0) ? -1 : 1;
          }
            break;
          case 4: /* Alternate +t and -t on all bytes */
          {
            for(z = 0; z < 32; z++)
              mapp[offset + z] += (z & 1 == 0) ? -t : t;
          }
            break;
          case 5: /* Replace every occurence of h with H */
          {
            if(mapp[offset + i1] == _104)
              mapp[offset + i1] = _72;

            if(mapp[offset + i2] == _104)
              mapp[offset + i2] = _72;

            if(mapp[offset + i3] == _104)
              mapp[offset + i3] = _72;

            if(mapp[offset + i4] == _104)
              mapp[offset + i4] = _72;
          }
            break;
          case 6: /* If byte a is > byte b, swap them. */
          {
            if(mapp[offset + i1] > mapp[offset + i3]) {
              bits = mapp[offset + i1];
              mapp[offset + i1] = mapp[offset + i3];
              mapp[offset + i3] = bits;
            }
          }
            break;
          default: /* XOR all bytes */
          {
            for(z = 1; z < 32; z++)
              mapp[offset + z] ^= mapp[offset + z - 1];
          }
		}
      }
    }

	*(out) = map + (index * TILE);
}

int is_solution(byte diff, byte* tile, byte* nonce)
{
	SHA256_CTX ictx;
	uint64_t index;
	byte hash[HASHLEN];

	sha256_init(&ictx);
	sha256_update(&ictx, nonce, HASHLEN);//hash nonce first because we dont want to allow caching of index computation
	sha256_update(&ictx, tile, TILE);
	sha256_final(&ictx, hash);

	return peach_eval(hash, diff) == 0;
}

/**
 * Mode 0: Mining
 * Mode 1: Validating
 *
 */
int peach(BTRAILER *bt, word32 difficulty, byte *haiku, word32 *hps, int mode)
{
   printf("Peach mode %i\n", mode);
   SHA256_CTX ictx, mctx; /* Index & Mining Contexts */

   uint32_t sm;

   byte * map, *cache, *tile, *tile2, diff, bt_hash[HASHLEN];
   diff = difficulty; /* down-convert passed-in 32-bit difficulty to 8-bit */
   printf("diff %i\n", diff);

   uint64_t j, h;
   h = 0;


/* Allocate MAP on the Heap */
   map = malloc(MAP_LENGTH);
   if(map == NULL) {
      if(Trace) plog("Fatal: Unable to allocate memory for map.\n");
      goto out;
   }
   memset(map, 0, MAP_LENGTH);

/* Allocate MAP cache on the Heap */
   cache = malloc(MAP);
   if(cache == NULL) {
	   if(Trace) plog("Fatal: Unable to allocate memory for cache.\n");
       	   goto out;
   }

   memset(cache, 0, MAP);
   long start = time(NULL);
   int solved = 0;

   for(;;)
   {
	   if(!Running && mode == 0) goto out; /* SIGTERM Received */

	   h += 1;

	   sm = 0;
	   tile = NULL;

	   if(mode == 0) {
		   /* In mode 0, add random haiku to the passed-in candidate block trailer */
		  memset(&bt->nonce[0], 0, HASHLEN);
		  trigg_gen(&bt->nonce[0]);
		  trigg_gen(&bt->nonce[16]);
	   }

	   sha256_init(&ictx);
	   sha256_update(&ictx, (byte *) bt, BTSIZE - 4 - HASHLEN);
	   sha256_final(&ictx, bt_hash);

	   for(int i=0;i<HASHLEN;i++)
		   if(i == 0){
			   sm = bt_hash[i];
		   }else{
			   sm *= bt_hash[i];
		   }

	   sm %= MAP;

	   get_tile(&tile, sm, bt->phash, map, cache);
     
	   for(j = 0; j < JUMP; j++)
	   {
		   sm = next_index(sm, tile, bt->nonce);
		   get_tile(&tile, sm, bt->phash, map, cache);
	   }

	   solved = is_solution(diff, tile, bt_hash);//include the mining address and transactions as part of the solution

	   if(mode == 1) { /* Just Validating, not Mining, check once and return */
		  trigg_expand2(bt->nonce, &haiku[0]);
		  if(Trace) plog("\nV:%s\n\n", haiku);

		  goto out;
	   }

	   if(solved)
	   { /* We're Mining & We Solved! */

		  byte v24haiku[256];
		  if(peach(bt, difficulty, &v24haiku[0], NULL, 1))
			  printf("????Validation failed IN THE CONTEXT??????\n");
		  long end = time(NULL);
		  long elapsed = end - start;
		  int cached = 0;
		  for (int i =0;i<MAP;i++)
			  if(cache[i])
				  cached++;
		  printf("Solved in %li seconds, %li iterations, %i cached\n", elapsed, h, cached);
		  *hps = h;
		  trigg_expand2(bt->nonce, &haiku[0]);
		  if(Trace) plog("\nS:%s\n\n", haiku);

		  goto out;
	   }
	}


out:
	if(map != NULL) free(map);
	if(cache != NULL) free(cache);

	map = cache = NULL;

	if(mode == 1 && solved == 0)
		printf("####Validation failed#####\n");

	if(mode != 1/*not validating*/ && !Running)
		return 1; /* SIGTERM RECEIVED */
	return solved ? 0 : 1;  /* Return 0 if valid, 1 if not valid */
} /* End v24() */
