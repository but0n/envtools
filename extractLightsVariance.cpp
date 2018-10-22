/*
  median_cut.cpp by Tobias Alexander Franke (tob@cyberhead.de) 2013
  See http://www.tobias-franke.eu/?dev
  BSD License (http://www.opensource.org/licenses/bsd-license.php)
  Copyright (c) 2013, Tobias Alexander Franke (tob@cyberhead.de)
*/

#include <algorithm>
#include <cassert>
#include <float.h>
#include <getopt.h>
#include <iostream>
#include <vector>

#include "Light"
#include "Math"
#include "SummedAreaTable"
#include "SummedAreaTableRegion"

#include "extractLightsMerge.cpp"
#include "extractLightsVarianceDebug.cpp"

#define TINYEXR_IMPLEMENTATION
#include "tinyexr/tinyexr.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

void print(const char *name, const std::vector<double> &array) {
  std::cout << name << std::endl;
  for (int i = 0; i < array.size(); i++) {
    std::cout << array[i] << std::endl;
  }
}

void printVariance(const SatRegionVector &regions) {
  for (int i = 0; i < regions.size(); i++) {
    printf("region %d %d %d : %d %d\n", i, regions[i]._x, regions[i]._y,
           regions[i]._w, regions[i]._h);
    printf("region %d : %f %f %f %f %f %f\n", i, regions[i]._sum,
           regions[i]._sum1, regions[i]._sum2, regions[i]._sum3,
           regions[i]._sum4, regions[i]._sum5);
    // printf("region %d %f %f %f\n", i, regions[i]._r, regions[i]._g,
    //        regions[i]._b);
  }
}

struct Image {
  int width;
  int height;
  float *data;
  int nc;
};

int load_image_exr(const char *filename, Image &image) {
  const char *err;
  int ret = LoadEXR(&image.data, &image.width, &image.height, filename, &err);
  if (ret != 0) {
    printf("%s", err);
    return 1;
  }
  image.nc = 4;
  return 0;
}

int load_image_hdr(const char *filename, Image &image) {

  int x, y, n;
  float *data = stbi_loadf(filename, &image.width, &image.height, &image.nc, 3);
  if (!data)
    return 1;

  image.data = data;
  image.nc = 3;
  return 0;
}

/**
 * Recursively split a region r and append new subregions
 * A and B to regions vector when at an end.
 */
void splitRecursive(const SatRegion &r, const uint n,
                    SatRegionVector &regions) {
  // check: can't split any further?
  if (r._w < 2 || r._h < 2 || n == 0) {
    // only now add region
    regions.push_back(r);
    return;
  }

  SatRegion A, B;

  if (r._w > r._h)
    r.split_w(A, B);
  else
    r.split_h(A, B);

  if (A._h > 2 && A._w > 2) {
    splitRecursive(A, n - 1, regions);
  }

  if (B._h > 2 && B._w > 2) {
    splitRecursive(B, n - 1, regions);
  }
}

/**
 * The median cut algorithm Or Variance Minimisation
 *
 * img - Summed area table of an image
 * n - number of subdivision, yields 2^n cuts
 * regions - an empty vector that gets filled with generated regions
 */
void medianVarianceCut(const SummedAreaTable &img, const uint n,
                       SatRegionVector &regions) {
  regions.clear();

  // insert entire image as start region
  SatRegion r;
  r.create(0, 0, img.width(), img.height(), &img);

  // recursively split into subregions
  splitRecursive(r, n, regions);
}

void outputJSON(const LightVector &lights, uint height, uint width,
                uint imageAreaSize, double luminanceSum, int numLights) {
  size_t i = 0;
  size_t numLightsMax = numLights > 0 ? numLights : lights.size();

  std::cout << "[";
  double globalVariance = luminanceSum / imageAreaSize;

  for (LightVector::const_iterator l = lights.begin();
       l != lights.end() && i < numLightsMax; ++l) {

    const double x = l->_centroidPosition[0];
    const double y = l->_centroidPosition[1];

    // under hemisphere, we cull
    if (y >= 0.5)
      continue;

    const double w = l->_w;
    const double h = l->_h;

    // convert x,y to direction
    Vec3d d;

    // https://www.shadertoy.com/view/4dsGD2
    // Desmos math demonstration / check
    // a,b,c => x,y,z direction axis
    // https://www.desmos.com/calculator/2niuw1lpm5
    double phi = (x * 2.0 * PI) - PI * 0.5;
    double theta = (1.0 - y) * PI;

    // Equation from http://graphicscodex.com  [sphry]
    d[0] = sin(theta) * cos(phi);
    d[1] = cos(theta);
    d[2] = sin(theta) * sin(phi);

    // normalize direction
    const double norm = d.normalize();

    // convert to float
    const float rCol = l->_rAverage;
    const float gCol = l->_gAverage;
    const float bCol = l->_bAverage;

    // 1 JSON object per light
    std::cout << "{";
    ;
    std::cout << " \"direction\": [" << d[0] << ", " << d[1] << ", " << d[2]
              << "], ";
    std::cout << " \"luminosity\": " << (l->_lumAverage) << ", ";
    std::cout << " \"color\": [" << rCol << ", " << gCol << ", " << bCol
              << "], ";
    std::cout << " \"area\": {\"x\":" << x << ", \"y\":" << y << ", \"w\":" << w
              << ", \"h\":" << h << "}, ";
    std::cout << " \"sum\": " << l->_sum << ", ";
    std::cout << " \"lum_ratio\": " << (l->_sum / luminanceSum) << ", ";
    std::cout << " \"variance\": " << (l->_variance) << ", ";
    std::cout << " \"error\": " << (l->_error ? 1 : 0) << " ";
    std::cout << " }" << std::endl;

    if (i < numLightsMax - 1) {

      std::cout << ",";
    }

    i++;
  }

  std::cout << "]";
}

////////////////////////////////////////////////
static int usage(const std::string &name) {
  std::cerr << "Usage: " << name
            << " [-a max_light_areas] [-l max_light_length] [-r ratioLight] "
               "[-n numCuts] [-m lightsNum] [-d] file.hdr"
            << std::endl;
  return 1;
}

////////////////////////////////////////////////
// The Real Deal
// some examples scripts here for multi or single update:
// https://gist.github.com/Kuranes/fa7466291c9fad3cdfb845f80fabe646
int main(int argc, char **argv) {
  // max area encased by light extracted, ratio of env map size
  // default is using Area of 1% of EnvMap as dir approx light
  // 32x32 pixels out of 4096x2048

  double ratioLengthSizeMax = 0.08;
  double ratioAreaSizeMax = 0.05;
  int numLights = 1;

  // Idea is to limit light extraction to analytic directional light
  //  So we must limite area and power extracted
  // as more the power means more difference with Env lighting
  // when we compute shadow as
  // "real time shadows =  lightEnv - LightExtracted"
  // ratioLight = luminanceLight / luminanceEnv
  float ratioLuminanceLight =
      0.5f; // ratio of lightExtracted On Global Illumination sum
  int numCuts =
      8; // number of division squared of the envmap of same lighting power

  int c;
  bool debug = false;

  while ((c = getopt(argc, argv, "a:dl:m:n:r:")) != -1) {
    switch (c) {
    case 'a':
      ratioAreaSizeMax = atof(optarg);
      break;
    case 'd':
      debug = true;
      break;
    case 'l':
      ratioLengthSizeMax = atof(optarg);
      break;
    case 'm':
      numLights = atoi(optarg);
      break;
    case 'n':
      numCuts = atoi(optarg);
      break;
    case 'r':
      ratioLuminanceLight = atof(optarg);
      break;

    default:
      return usage(argv[0]);
    }
  }

  if (optind < argc) {
    ////////////////////////////////////////////////
    // load image
    int width, height, nc;
    float *rgba;

    Image image;

    if (load_image_exr(argv[optind], image) != 0) {
      std::cerr << "Cannot open " << argv[1] << " image file" << std::endl;
      return 1;
    }

    width = image.width;
    height = image.height;
    nc = image.nc;
    const uint imageAreaSize = image.width * image.height;
    rgba = image.data;

    ////////////////////////////////////////////////
    // create summed area table of luminance image
    SummedAreaTable lum_sat;

    lum_sat.createLum(rgba, width, height, nc);
#if 0
    print("sat", lum_sat._sat);
    print("sat1", lum_sat._sat1);
    print("sat2", lum_sat._sat2);
    print("sat3", lum_sat._sat3);
    print("sat4", lum_sat._sat4);
    print("sat5", lum_sat._sat5);

    print("r", lum_sat._r);
    print("g", lum_sat._g);
    print("b", lum_sat._b);
#endif
    ////////////////////////////////////////////////
    // apply cut algorithm
    SatRegionVector regions;

    medianVarianceCut(lum_sat, numCuts, regions); // max 2^n cuts

    printVariance(regions);

    if (regions.empty()) {
      std::cerr << "Cannot cut " << argv[1] << " into light regions"
                << std::endl;
      return 1;
    }

    ////////////////////////////////////////////////
    // create Lights from regions
    LightVector lights;
    LightVector mainLights;

    //
    // convert absolute input parameters
    // to relative to environment at hand value.
    // From ratio to pixel squared area
    /// Light Max luminance in percentage
    double luminanceSum = lum_sat.getSum();
    lum_sat.sum(0, 0, width - 1, 0, width - 1, height - 1, 0, height - 1);

    const double luminanceMaxLight = ratioLuminanceLight * luminanceSum;

    // And he saw that light was good, and separated light from darkness
    createLightsFromRegions(regions, lights, rgba, luminanceMaxLight, width,
                            height, nc, lum_sat);

    // sort lights
    // the smaller, the more powerful luminance
    std::sort(lights.begin(), lights.end());

    const double areaSizeMax = ratioAreaSizeMax;
    const double degreeMerge = 35.0;

#define MERGE 1
#ifdef MERGE

    // Light Area Size under which we merge
    // default to size of the median region size
    // if lots of small lights => give small area
    // if lots of big lights => give big area
    // const uint mergeindexPos =  (lights.size() * 25) / 100;
    // const double mergeAreaSize = lights[mergeindexPos]._areaSize;

    const double mergeAreaSize = areaSizeMax;

    uint mergedLights =
        mergeLights(lights, mainLights, width, height, mergeAreaSize,
                    ratioLengthSizeMax, luminanceMaxLight, degreeMerge);

    // sort By sum now (changed the sort Criteria during merge)
    // biggest Sum first
    std::sort(mainLights.begin(), mainLights.end());
    std::reverse(mainLights.begin(), mainLights.end());

//#define SELECT 1
#ifdef SELECT

    // now keep lights from inside merged Zone
    lights.clear();

    mergedLights = selectLights(mainLights, lights, width, height, areaSizeMax,
                                luminanceMaxLight, luminanceSum, degreeMerge);

    mainLights.clear();
    mainLights.resize(lights.size());
    std::copy(lights.begin(), lights.end(), mainLights.begin());

    // sort By sum now (changed the sort Criteria during merge)
    // biggest sum first
    std::sort(mainLights.begin(), mainLights.end());
    std::reverse(mainLights.begin(), mainLights.end());

#endif // SELECT

//#define NEARLIGHT_MERGE 1
#ifdef NEARLIGHT_MERGE

    // now keep lights from inside merged Zone
    // lights.clear();

    mergeNearLights(mainLights, areaSizeMax, ratioLengthSizeMax, degreeMerge);

    // sort By sum now (changed the sort Criteria during merge)
    // biggest sum first
    std::sort(mainLights.begin(), mainLights.end());
    std::reverse(mainLights.begin(), mainLights.end());
    std::copy(mainLights.begin(), mainLights.end(), lights.begin());

#endif // NEARLIGHT_MERGE

#else

    mainLights.resize(lights.size());
    std::copy(lights.begin(), lights.end(), mainLights.begin());

#endif

    ////////////////////////////////////////////////
    // output JSON

    // do we want to output/save original same variance light ?
    // Merged Light sorted By Area Size
    // outputJSON(lights, height, width, imageAreaSize );

    // Merged Light sorted By Luminance intensity
    outputJSON(mainLights, height, width, imageAreaSize, luminanceSum,
               numLights);

    if (debug) {
      debugDrawLight(regions, lights, mainLights, rgba, width, height, nc,
                     lum_sat.getMaxLum(), lum_sat.getMinLum(), numLights);
    }

  } else {

    return usage(argv[0]);
  }

  return 0;
}
