//
// Created by Richard Gowers on 8/13/20.
//

#include <immintrin.h>
#include <iostream>

#include "vanilla.h"

static void printX(const char* name, __m128 val) {
    float x[4];

    _mm_storeu_ps(x, val);

    std::cout << name << " ";
    for (unsigned int i=0; i<4; ++i)
        std::cout << x[i] << " ";
    std::cout << std::endl;
}

// [X1, Y1, Z1, X2], [Y2, Z2, X3, Y3], [Z3, X4, Y4, Z4]
// TO
// [X1, X2, X3, X4], [Y1, Y2, Y3, Y4], [Z1, Z2, Z3, Z4]
#define AoS2SoA(v1, v2, v3) \
do {\
  __m128 tmp1, tmp2; \
  tmp1 = _mm_shuffle_ps(v1, v2, _MM_SHUFFLE(1, 0, 2, 1)); \
  tmp2 = _mm_shuffle_ps(v2, v3, _MM_SHUFFLE(2, 1, 3, 2)); \
  (v1) = _mm_shuffle_ps(v1, tmp2, _MM_SHUFFLE(2, 0, 3, 0)); \
  (v2) = _mm_shuffle_ps(tmp1, tmp2, _MM_SHUFFLE(3, 1, 2, 0)); \
  (v3) = _mm_shuffle_ps(tmp1, v3, _MM_SHUFFLE(3, 0, 3, 1)); \
} while (0)

// zip over coords1 and coords2 and calculate pairwise distance w/ periodic boundary conditions
// store results in output, must be large enough etc etc
void XCalcBonds(const float* coords1,
                const float* coords2,
                const float* box,
                unsigned int nvals,
                float* output) {
  __m128 Xb1, Xb2, Xb3;
  __m128 ib1, ib2, ib3;  // inverse box lengths
  Xb1 = _mm_set_ps1(box[0]);
  Xb2 = _mm_set_ps1(box[1]);
  Xb3 = _mm_set_ps1(box[2]);
  ib1 = _mm_set_ps1(1/box[0]);
  ib2 = _mm_set_ps1(1/box[1]);
  ib3 = _mm_set_ps1(1/box[2]);

    // deal with single iterations
    unsigned int nsingle = nvals & 0x03;
    CalcBonds(coords1, coords2, box, nsingle, output);

    coords1 += nsingle*3;
    coords2 += nsingle*3;
    output += nsingle;

    unsigned int niters = nvals >> 2;
    for (unsigned int i=0; i<niters; ++i) {
      // load 4 coords from each
      __m128 p1, p2, p3, p4, p5, p6;
      p1 = _mm_loadu_ps(coords1 + i*12);
      p2 = _mm_loadu_ps(coords1 + i*12 + 4);
      p3 = _mm_loadu_ps(coords1 + i*12 + 8);
      p4 = _mm_loadu_ps(coords2 + i*12);
      p5 = _mm_loadu_ps(coords2 + i*12 + 4);
      p6 = _mm_loadu_ps(coords2 + i*12 + 8);

      AoS2SoA(p1, p2, p3);
      AoS2SoA(p4, p5, p6);

      // calculate deltas
      p1 = _mm_sub_ps(p1, p4);  // dx
      p2 = _mm_sub_ps(p2, p5);  // dy
      p3 = _mm_sub_ps(p3, p6);  // dz

      // apply periodic boundary conditions
      // rij = rij - box * rint(rij / box)
      __m128 adj1, adj2, adj3;
      adj1 = _mm_mul_ps(Xb1, _mm_round_ps(_mm_mul_ps(p1, ib1), (_MM_ROUND_NEAREST|_MM_FROUND_NO_EXC)));
      adj2 = _mm_mul_ps(Xb2, _mm_round_ps(_mm_mul_ps(p2, ib2), (_MM_ROUND_NEAREST|_MM_FROUND_NO_EXC)));
      adj3 = _mm_mul_ps(Xb3, _mm_round_ps(_mm_mul_ps(p3, ib3), (_MM_ROUND_NEAREST|_MM_FROUND_NO_EXC)));

      p1 = _mm_sub_ps(p1, adj1);
      p2 = _mm_sub_ps(p2, adj2);
      p3 = _mm_sub_ps(p3, adj3);

      // square each
      p1 = _mm_mul_ps(p1, p1);
      p2 = _mm_mul_ps(p2, p2);
      p3 = _mm_mul_ps(p3, p3);

      // summation time
      __m128 rsq = _mm_add_ps(p1, _mm_add_ps(p2, p3));
      __m128 r = _mm_sqrt_ps(rsq);

      _mm_storeu_ps(output, r);
      output += 4;
    }
}

// identical to _MM_TRANSPOSE4_ps except we don't care about the last column in input, i.e. final row in output
#define _MM_TRANSPOSE(row0, row1, row2, row3) \
do { \
  __m128 tmp3, tmp2, tmp1, tmp0; \
  tmp0 = _mm_unpacklo_ps((row0), (row1)); \
  tmp2 = _mm_unpacklo_ps((row2), (row3)); \
  tmp1 = _mm_unpackhi_ps((row0), (row1)); \
  tmp3 = _mm_unpackhi_ps((row2), (row3)); \
  (row0) = _mm_movelh_ps(tmp0, tmp2); \
  (row1) = _mm_movehl_ps(tmp2, tmp0); \
  (row2) = _mm_movelh_ps(tmp1, tmp3); \
} while (0)
  //  (row3) = _mm_movehl_ps(tmp3, tmp1);


// Read *Ncoords* pairs of indices from idx and calculate pairwise distance betwixt
void XCalcBondsIdx(const float* coords,
                   const unsigned int* idx,  // holds [[1, 2], [7, 8], etc]
                   const float* box,
                   unsigned int Ncoords,
                   float* output) {
  __m128 b[3], ib[3];
  for (unsigned char i=0; i<3; ++i) {
    // b[0] = [lx, lx, lx, lx], ib == inverse box
    b[i] = _mm_set_ps1(box[i]);
    ib[i] = _mm_set_ps1(1 / box[i]);
  }

  // TODO: Single interations
  unsigned int niters = Ncoords >> 2;
  for (unsigned int i=0; i<niters; ++i) {
    __m128 p1[4];
    __m128 p2[4];
    for (unsigned char j=0; j<4; ++j) {
      unsigned int a = idx[i*8 + j*2];
      unsigned int b = idx[i*8 + j*2 + 1];
      p1[j] = _mm_loadu_ps(coords + a*3);
      p2[j] = _mm_loadu_ps(coords + b*3);
    }
    _MM_TRANSPOSE(p1[0], p1[1], p1[2], p1[3]);
    _MM_TRANSPOSE(p2[0], p2[1], p2[2], p2[3]);
    // p1[0] x coordinate of each, 1=y, 2=z, p1[3] now meaningless
    __m128 delta[3];
    for (unsigned char j=0; j<3; ++j)
      delta[j] = _mm_sub_ps(p1[j], p2[j]);

    // apply minimum image convention
    for (unsigned char j=0; j<3; ++j) {
      __m128 adj = _mm_mul_ps(b[j], _mm_round_ps(_mm_mul_ps(delta[j], ib[j]), (_MM_ROUND_NEAREST | _MM_FROUND_NO_EXC)));
      delta[j] = _mm_sub_ps(delta[j], adj);
    }

    // square each and sum
    for (unsigned char j=0; j<3; ++j)
      delta[j] = _mm_mul_ps(delta[j], delta[j]);
    delta[0] = _mm_add_ps(delta[0], delta[1]);
    delta[0] = _mm_add_ps(delta[0], delta[2]);

    __m128 r = _mm_sqrt_ps(delta[0]);

    _mm_storeu_ps(output, r);
    output += 4;
  }
}