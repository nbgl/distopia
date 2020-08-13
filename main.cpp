#include <iostream>
#include <math.h>
#include <immintrin.h>
#include <chrono>

#include "XMMdist.h"
#include "vanilla.h"

bool loadHeader(FILE* fp, int* Ncoords, float* box) {
  // header format:
  // natoms
  // boxx, boxy, boxz
  char tmp[1024];

  if (!fgets(tmp, 1024, fp))
    abort();

  *Ncoords = strtol(tmp, NULL, 10);

  fgets(tmp, 1024, fp);
  char* next = tmp;
  for (unsigned i=0; i<3; ++i)
    *box++ = strtof(next, &next);

  return true;
}

bool loadCoords(FILE* fp, int Ncoords, float* coords) {
  char tmp[4096];

  for (unsigned int i=0; i<Ncoords; ++i) {
    fgets(tmp, 4096, fp);
    char* next = tmp;
    for (unsigned char j=0; j<3; ++j)
      *coords++ = strtof(next, &next);
  }

  return true;
}

int main(int argc, char* argv[]) {
  // usage: file.in
  char* fname = argv[1];

  float box[3];
  float *coords, *coords1, *coords2, *results;
  int Ncoords=0;

  FILE* fp = fopen(fname, "r");
  if (!fp)
      return 1;
   if(!loadHeader(fp, &Ncoords, box))
       return 2;

  coords = (float*) malloc(Ncoords * 3 * sizeof(float));
  results = (float*) malloc(Ncoords * sizeof(float) /2);

  loadCoords(fp, Ncoords, coords);

  std::cout << "Read " << Ncoords << " coordinates \n";

  // split coordinates in half
  coords1 = coords;
  coords2 = coords + (3*Ncoords/2);
  int Nresults = Ncoords/2;

  std::chrono::steady_clock::time_point t1, t2;
  std::chrono::duration<double> dt;

  t1 = std::chrono::steady_clock::now();

  CalcBonds(coords1, coords2, box, Nresults, results);

  t2 = std::chrono::steady_clock::now();

  dt = (t2 - t1);
  std::cout << "Regular calc_bonds: " << dt.count() << "\n";

  t1 = std::chrono::steady_clock::now();

  XCalcBonds(coords1, coords2, box, Nresults, results);

  t2 = std::chrono::steady_clock::now();

  dt = (t2 - t1);

  std::cout << "XMM calc_bonds: " << dt.count() << "\n";

  return 0;
}