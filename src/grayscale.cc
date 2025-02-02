#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "main.hh"
#include "tiles.hh"

int loadClutter(char *filename, double radius, struct site tx)
{
  /* This function reads a MODIS 17-class clutter file in ASCII Grid format.
     The nominal heights it applies to each value, eg. 5 (Mixed forest) = 15m are
     taken from ITU-R P.452-11.
     It doesn't have it's own matrix, instead it boosts the DEM matrix like point clutter
     AddElevation(lat, lon, height);
     If tiles are standard 2880 x 3840 then cellsize is constant at 0.004166
   */
  int x, y, z, h, w;
  // int  result;

  double clh, xll, yll, cellsize, cellsize2, xOffset, yOffset, lat, lon;
  // double xur, yur, i, j;

  char line[50000];
  char *pch;
  FILE *fd;

  if ((fd = fopen(filename, "rb")) == NULL) return errno;

  if ((fgets(line, 19, fd)) != NULL) {
    pch = strtok(line, " ");
    pch = strtok(NULL, " ");
    w = atoi(pch);
  }

  if (fgets(line, 19, fd) != NULL) {
    // pch = strtok (line," ");
    // pch = strtok (NULL, " ");
    h = atoi(pch);
  }

  if (w == 2880 && h == 3840) {
    cellsize = 0.004167;
    cellsize2 = cellsize * 3;
  }
  else {
    return 0;  // can't work with this yet
  }
  if (debug) {
    fprintf(stderr, "\nLoading clutter file \"%s\" %d x %d...\n", filename, w, h);
    fflush(stderr);
  }
  if (fgets(line, 25, fd) != NULL) {
    sscanf(pch, "%lf", &xll);
  }

  fgets(line, 25, fd);
  if (fgets(line, 25, fd) != NULL) {
    sscanf(pch, "%lf", &yll);
  }

  if (debug) {
    fprintf(stderr, "\nxll %.2f yll %.2f\n", xll, yll);
    fflush(stderr);
  }

  fgets(line, 25, fd);  // cellsize

  // loop over matrix
  for (y = h; y > 0; y--) {
    x = 0;
    if (fgets(line, 100000, fd) != NULL) {
      pch = strtok(line, " ");
      while (pch != NULL && x < w) {
        z = atoi(pch);

        // Apply ITU-R P.452-11
        // Treat classes 0, 9, 10, 11, 15, 16 as water, (Water, savanna, grassland, wetland, snow, barren)
        clh = 0.0;

        // evergreen, evergreen, urban
        if (z == 1 || z == 2 || z == 13) clh = 20.0;
        // deciduous, deciduous, mixed
        if (z == 3 || z == 4 || z == 5) clh = 15.0;
        // woody shrublands & savannas
        if (z == 6 || z == 8) clh = 4.0;
        // shurblands, savannas, croplands...
        if (z == 7 || z == 9 || z == 10 || z == 12 || z == 14) clh = 2.0;

        if (clh > 1) {
          xOffset = x * cellsize;  // 12 deg wide
          yOffset = y * cellsize;  // 16 deg high

          // make all longitudes positive
          if (xll + xOffset > 0) {
            lon = 360 - (xll + xOffset);
          }
          else {
            lon = (xll + xOffset) * -1;
          }
          lat = yll + yOffset;

          // bounding box
          if (lat > tx.lat - radius && lat < tx.lat + radius && lon > tx.lon - radius && lon < tx.lon + radius) {
            // not in near field
            if ((lat > tx.lat + cellsize2 || lat < tx.lat - cellsize2) ||
                (lon > tx.lon + cellsize2 || lon < tx.lon - cellsize2)) {
              AddElevation(lat, lon, clh, 2);
            }
          }
        }

        x++;
        pch = strtok(NULL, " ");
      }  // while
    }
    else {
      fprintf(stderr, "Clutter error @ x %d y %d\n", x, y);
    }  // if
  }    // for

  fclose(fd);
  return 0;
}

int loadLIDAR(char *filenames, int resample)
{
  char *filename;
  char *files[900];  // 20x20=400, 16x16=256 tiles
  int indx = 0, fc = 0, hoffset = 0, voffset = 0, pos, success;
  //	double xll, yll, xur, yur, cellsize,
  double avgCellsize = 0, smCellsize = 0;
  //	char found,
  //	char free_page = 0, jline[20], lid_file[255],
  //	path_plus_name[255], *junk = NULL;
  //	char line[25000];
  //	char * pch;
  //    	double TO_DEG = (180 / PI);
  //	FILE *fd;
  tile_t *tiles;

  // test for multiple files
  filename = strtok(filenames, " ,");
  while (filename != NULL) {
    files[fc] = filename;
    filename = strtok(NULL, " ,");
    fc++;
  }

  /* Allocate the tile array */
  if ((tiles = (tile_t *)calloc(fc + 1, sizeof(tile_t))) == NULL) return ENOMEM;

  /* Load each tile in turn */
  for (indx = 0; indx < fc; indx++) {
    /* Grab the tile metadata */
    if ((success = tile_load_lidar(&tiles[indx], files[indx])) != 0) {
      fprintf(stderr, "Failed to load LIDAR tile %s\n", files[indx]);
      fflush(stderr);
      free(tiles);
      return success;
    }

    if (debug) {
      fprintf(stderr, "Loading \"%s\" into page %d with width %d...\n", files[indx], indx, tiles[indx].width);
      fflush(stderr);
    }

    // Increase the "average" cell size
    avgCellsize += tiles[indx].cellsize;
    // Update the smallest cell size
    if (smCellsize == 0 || tiles[indx].cellsize < smCellsize) {
      smCellsize = tiles[indx].cellsize;
    }

    // Update a bunch of globals
    if (tiles[indx].max_el > max_elevation) max_elevation = tiles[indx].max_el;
    if (tiles[indx].min_el < min_elevation) min_elevation = tiles[indx].min_el;

    if (max_north == -90 || tiles[indx].max_north > max_north) max_north = tiles[indx].max_north;

    if (min_north == 90 || tiles[indx].min_north < min_north) min_north = tiles[indx].min_north;

    if (tiles[indx].max_west > max_west) max_west = tiles[indx].max_west;
    if (tiles[indx].min_west < min_west) min_west = tiles[indx].min_west;

    if (max_west == -1) {
      max_west = tiles[indx].max_west;
    }
    else {
      if (abs(tiles[indx].max_west - max_west) < 180) {
        if (tiles[indx].max_west > max_west) max_west = tiles[indx].max_west;
      }
      else {
        if (tiles[indx].max_west < max_west) max_west = tiles[indx].max_west;
      }
    }

    if (min_west == 360) {
      min_west = tiles[indx].min_west;
    }
    else {
      if (fabs(tiles[indx].min_west - min_west) < 180.0) {
        if (tiles[indx].min_west < min_west) min_west = tiles[indx].min_west;
      }
      else {
        if (tiles[indx].min_west > min_west) min_west = tiles[indx].min_west;
      }
    }
  }

  /* Iterate through all of the tiles to find the smallest resolution. We will
   * need to rescale every tile from here on out to this value */
  float smallest_res = 0;
  for (size_t i = 0; i < fc; i++) {
    if (smallest_res == 0 || tiles[i].resolution < smallest_res) {
      smallest_res = tiles[i].resolution;
    }
  }

  /* Now we need to rescale all tiles the the lowest resolution or the requested resolution. ie if we have
   * one 1m lidar and one 2m lidar, resize the 2m to fake 1m */
  float desired_resolution = resample != 0 && smallest_res < resample ? resample : smallest_res;
  if (desired_resolution > resample && resample != 0 && debug)
    fprintf(stderr, "Warning: Unable to rescale to requested resolution\n");
  for (size_t i = 0; i < fc; i++) {
    float rescale = tiles[i].resolution / (float)desired_resolution;
    if (debug) fprintf(stderr, "res %.2f desired_res %.2f\n", tiles[i].resolution, (float)desired_resolution);
    if (rescale != 1) {
      if ((success = tile_rescale(&tiles[i], rescale) != 0)) {
        fprintf(stderr, "Error resampling tiles\n");
        return success;
      }
    }
  }

  /* Now we work out the size of the giant lidar tile. */
  double total_width = max_west - min_west >= 0 ? max_west - min_west : max_west + (360 - min_west);
  double total_height = max_north - min_north;
  if (debug) {
    fprintf(stderr, "totalh: %.7f - %.7f = %.7f totalw: %.7f - %.7f = %.7f\n", max_north, min_north, total_height, max_west,
            min_west, total_width);
    fprintf(stderr, "mw:%lf Mnw:%lf\n", max_west, min_west);
  }
  // detect problematic layouts eg. vertical rectangles
  // create a synthetic empty tile on the fly with dimensions to make a square
  /*if(total_height > total_width*1.5){
          tiles[fc].max_north=max_north;
          tiles[fc].min_north=min_north;
          westoffset=westoffset-total_width; // WGS84 for stdout only
          max_west=max_west+total_width; // Positive westing
          tiles[fc].max_west=max_west; // Positive westing
          tiles[fc].min_west=max_west;
          tiles[fc].ppdy=tiles[fc-1].ppdy;
          tiles[fc].ppdy=tiles[fc-1].ppdx;
          tiles[fc].width=tiles[fc-1].width;
          tiles[fc].height=total_height;
          tiles[fc].data=tiles[fc-1].data;
          fc++;
  }*/

  /* This is how we should _theoretically_ work this out, but due to
   * the nature of floating point arithmetic and rounding errors, 499 usd to gbpwe need to
   * crunch the numbers the hard way */
  // size_t new_width = total_width * pix_per_deg;
  // size_t new_height = total_height * pix_per_deg;

  size_t new_height = 0;
  size_t new_width = 0;
  for (size_t i = 0; i < fc; i++) {
    double north_offset = max_north - tiles[i].max_north;
    double west_offset =
        max_west - tiles[i].max_west >= 0 ? max_west - tiles[i].max_west : max_west + (360 - tiles[i].max_west);
    size_t north_pixel_offset = north_offset * tiles[i].ppdy;
    size_t west_pixel_offset = west_offset * tiles[i].ppdx;

    if (west_pixel_offset + tiles[i].width > new_width) new_width = west_pixel_offset + tiles[i].width;
    if (north_pixel_offset + tiles[i].height > new_height) new_height = north_pixel_offset + tiles[i].height;
    if (debug) fprintf(stderr, "north_pixel_offset %d west_pixel_offset %d\n", north_pixel_offset, west_pixel_offset);
  }

  size_t new_tile_alloc = new_width * new_height;

  short *new_tile = (short *)calloc(new_tile_alloc, sizeof(short));
  if (new_tile == NULL) {
    free(tiles);
    return ENOMEM;
  }
  if (debug) fprintf(stderr, "Lidar tile dimensions w:%lf(%zu) h:%lf(%zu)\n", total_width, new_width, total_height, new_height);

  /* ...If we wanted a value other than sea level here, we would need to initialize the array... */
  size_t prevPixelOffsetW = 0;
  size_t prevPixelOffsetN = 0;
  /* Fill out the array one tile at a time */
  for (size_t i = 0; i < fc; i++) {
    double north_offset = max_north - tiles[i].max_north;
    double west_offset =
        max_west - tiles[i].max_west >= 0 ? max_west - tiles[i].max_west : max_west + (360 - tiles[i].max_west);
    size_t north_pixel_offset = north_offset * tiles[i].ppdy;
    size_t west_pixel_offset = west_offset * tiles[i].ppdx;
    /*if(i>0){
            if(tiles[i].ppdx>tiles[i-1].ppdx+10 || tiles[i].ppdx<tiles[i-1].ppdx-10)
                    west_pixel_offset=prevPixelOffsetW; //PROBLEM WITH DIFF SIZED TILES eg. 4096, 4069..
            if(tiles[i].ppdy>tiles[i-1].ppdy+10 || tiles[i].ppdy<tiles[i-1].ppdy-10)
                    north_pixel_offset=prevPixelOffsetW; //PROBLEM WITH DIFF SIZED TILES eg. 4096, 4069..
    }*/
    prevPixelOffsetW = west_pixel_offset;
    prevPixelOffsetN = north_pixel_offset;

    if (debug) {
      fprintf(stderr, "mn: %lf mw:%lf globals: %lf %lf\n", tiles[i].max_north, tiles[i].max_west, max_north, max_west);
      fprintf(stderr, "Offset n:%zu(%lf) w:%zu(%lf)\n", north_pixel_offset, north_offset, west_pixel_offset, west_offset);
      fprintf(stderr, "Height: %d\n", tiles[i].height);
    }

    /* Copy it row-by-row from the tile */
    for (size_t h = 0; h < tiles[i].height; h++) {
      short *dest_addr = &new_tile[(north_pixel_offset + h) * new_width + west_pixel_offset];
      short *src_addr = &tiles[i].data[h * tiles[i].width];
      // Check if we might overflow
      if (dest_addr + tiles[i].width > new_tile + new_tile_alloc || dest_addr < new_tile) {
        if (debug) fprintf(stderr, "Overflow %zu\n", i);
        continue;
      }
      memcpy(dest_addr, src_addr, tiles[i].width * sizeof(short));
    }
  }

  // SUPER tile
  MAXPAGES = 1;
  IPPD = MAX(new_width, new_height);

  if (debug) {
    fprintf(stderr, "Setting IPPD to %d\n", IPPD);
    fflush(stderr);
  }
  ARRAYSIZE = (MAXPAGES * IPPD) + 50;
  do_allocs();

  /* Load the data into the global dem array */
  dem[0].max_north = max_north;
  dem[0].min_west = min_west;
  dem[0].min_north = min_north;
  dem[0].max_west = max_west;
  dem[0].max_el = max_elevation;
  dem[0].min_el = min_elevation;

  /*
   * Copy the lidar tile data into the dem array. The dem array is rotated
   * 90 degrees (christ knows why...)
   */
  int y = new_height - 1;
  for (size_t h = 0; h < new_height; h++, y--) {
    int x = new_width - 1;
    for (size_t w = 0; w < new_width; w++, x--) {
      dem[0].data[y][x] = new_tile[h * new_width + w];
      dem[0].signal[y][x] = 0;
      dem[0].mask[y][x] = 0;
    }
  }

  ippd = IPPD;
  height = new_height;
  width = new_width;

  if (debug) fprintf(stderr, "LIDAR LOADED %d x %d\n", width, height);

  if (debug)
    fprintf(stderr, "fc %d WIDTH %d HEIGHT %d ippd %d minN %.5f maxN %.5f minW %.5f maxW %.5f avgCellsize %.5f\n", fc, width,
            height, ippd, min_north, max_north, min_west, max_west, avgCellsize);

  /* cleanup:

          if ( tiles != NULL ) {
                  for (size_t i = 0; i < fc-1; i++) {
                          tile_destroy(&tiles[i]);
                  }
          }
          free(tiles);
  */
  return 0;
}

int LoadSDF_SDF(char *name)
{
  /* This function reads uncompressed ss Data Files (.sdf)
     containing digital elevation model data into memory.
     Elevation data, maximum and minimum elevations, and
     quadrangle limits are stored in the first available
     dem[] structure.
     NOTE: On error, this function returns a negative errno */

  int x, y, data = 0, indx, minlat, minlon, maxlat, maxlon, j;
  char found, free_page = 0, line[20], jline[20], sdf_file[255], path_plus_name[PATH_MAX];

  FILE *fd;

  for (x = 0; name[x] != '.' && name[x] != 0 && x < 250; x++) sdf_file[x] = name[x];

  sdf_file[x] = 0;

  /* Parse filename for minimum latitude and longitude values */

  if (sscanf(sdf_file, "%d:%d:%d:%d", &minlat, &maxlat, &minlon, &maxlon) != 4) return -EINVAL;

  sdf_file[x] = '.';
  sdf_file[x + 1] = 's';
  sdf_file[x + 2] = 'd';
  sdf_file[x + 3] = 'f';
  sdf_file[x + 4] = 0;

  /* Is it already in memory? */

  for (indx = 0, found = 0; indx < MAXPAGES && found == 0; indx++) {
    if (minlat == dem[indx].min_north && minlon == dem[indx].min_west && maxlat == dem[indx].max_north &&
        maxlon == dem[indx].max_west)
      found = 1;
  }

  /* Is room available to load it? */

  if (found == 0) {
    for (indx = 0, free_page = 0; indx < MAXPAGES && free_page == 0; indx++)
      if (dem[indx].max_north == -90) free_page = 1;
  }

  indx--;

  if (free_page && found == 0 && indx >= 0 && indx < MAXPAGES) {
    /* Search for SDF file in current working directory first */

    strncpy(path_plus_name, sdf_file, sizeof(path_plus_name) - 1);

    if ((fd = fopen(path_plus_name, "rb")) == NULL) {
      /* Next, try loading SDF file from path specified
         in $HOME/.ss_path file or by -d argument */

      strncpy(path_plus_name, sdf_path, sizeof(path_plus_name) - 1);
      strncat(path_plus_name, sdf_file, sizeof(path_plus_name) - 1);
      if ((fd = fopen(path_plus_name, "rb")) == NULL) {
        return -errno;
      }
    }

    if (debug == 1) {
      fprintf(stderr, "Loading \"%s\" into page %d...\n", path_plus_name, indx + 1);
      fflush(stderr);
    }

    if (fgets(line, 19, fd) != NULL) {
      if (sscanf(line, "%f", &dem[indx].max_west) == EOF) return -errno;
    }

    if (fgets(line, 19, fd) != NULL) {
      if (sscanf(line, "%f", &dem[indx].min_north) == EOF) return -errno;
    }

    if (fgets(line, 19, fd) != NULL) {
      if (sscanf(line, "%f", &dem[indx].min_west) == EOF) return -errno;
    }

    if (fgets(line, 19, fd) != NULL) {
      if (sscanf(line, "%f", &dem[indx].max_north) == EOF) return -errno;
    }
    /*
       Here X lines of DEM will be read until IPPD is reached.
       Each .sdf tile contains 1200x1200 = 1.44M 'points'
       Each point is sampled for 1200 resolution!
     */
    for (x = 0; x < ippd; x++) {
      for (y = 0; y < ippd; y++) {
        for (j = 0; j < jgets; j++) {
          if (fgets(jline, sizeof(jline), fd) == NULL) return -EIO;
        }

        if (fgets(line, sizeof(line), fd) != NULL) {
          data = atoi(line);
        }

        dem[indx].data[x][y] = data;
        dem[indx].signal[x][y] = 0;
        dem[indx].mask[x][y] = 0;

        if (data > dem[indx].max_el) dem[indx].max_el = data;

        if (data < dem[indx].min_el) dem[indx].min_el = data;
      }

      if (ippd == 600) {
        for (j = 0; j < IPPD; j++) {
          if (fgets(jline, sizeof(jline), fd) == NULL) return -EIO;
        }
      }
      if (ippd == 300) {
        for (j = 0; j < IPPD; j++) {
          if (fgets(jline, sizeof(jline), fd) == NULL) return -EIO;
          if (fgets(jline, sizeof(jline), fd) == NULL) return -EIO;
          if (fgets(jline, sizeof(jline), fd) == NULL) return -EIO;
        }
      }
    }

    fclose(fd);

    dem[indx].min_x = rint(ippd * dem[indx].min_north);
    dem[indx].max_x = rint(ippd * dem[indx].max_north);
    dem[indx].min_y = rint(ippd * dem[indx].min_west);
    dem[indx].max_y = rint(ippd * dem[indx].max_west);

    if (dem[indx].min_el < min_elevation) min_elevation = dem[indx].min_el;

    if (dem[indx].max_el > max_elevation) max_elevation = dem[indx].max_el;

    if (max_north == -90)
      max_north = dem[indx].max_north;

    else if (dem[indx].max_north > max_north)
      max_north = dem[indx].max_north;

    if (min_north == 90)
      min_north = dem[indx].min_north;

    else if (dem[indx].min_north < min_north)
      min_north = dem[indx].min_north;

    if (max_west == -1)
      max_west = dem[indx].max_west;

    else {
      if (abs(dem[indx].max_west - max_west) < 180) {
        if (dem[indx].max_west > max_west) max_west = dem[indx].max_west;
      }

      else {
        if (dem[indx].max_west < max_west) max_west = dem[indx].max_west;
      }
    }

    if (min_west == 360)
      min_west = dem[indx].min_west;

    else {
      if (fabs(dem[indx].min_west - min_west) < 180.0) {
        if (dem[indx].min_west < min_west) min_west = dem[indx].min_west;
      }

      else {
        if (dem[indx].min_west > min_west) min_west = dem[indx].min_west;
      }
    }

    return 1;
  }

  else
    return 0;
}

int LoadSDF(char *name)
{
  /* This function loads the requested SDF file from the filesystem.
     It first tries to invoke the LoadSDF_SDF() function to load an
     uncompressed SDF file (since uncompressed files load slightly
     faster).  If that attempt fails, then it tries to load a
     compressed SDF file by invoking the LoadSDF_BZ() function.
     If that fails, then we can assume that no elevation data
     exists for the region requested, and that the region
     requested must be entirely over water. */

  int x, y, indx, minlat, minlon, maxlat, maxlon;
  char found, free_page = 0;
  int return_value = -1;

  return_value = LoadSDF_SDF(name);

  /* If neither format can be found, then assume the area is water. */

  if (return_value == 0 || return_value < 0) {
    sscanf(name, "%d:%d:%d:%d", &minlat, &maxlat, &minlon, &maxlon);

    /* Is it already in memory? */

    for (indx = 0, found = 0; indx < MAXPAGES && found == 0; indx++) {
      if (minlat == dem[indx].min_north && minlon == dem[indx].min_west && maxlat == dem[indx].max_north &&
          maxlon == dem[indx].max_west)
        found = 1;
    }

    /* Is room available to load it? */

    if (found == 0) {
      for (indx = 0, free_page = 0; indx < MAXPAGES && free_page == 0; indx++)
        if (dem[indx].max_north == -90) free_page = 1;
    }

    indx--;

    if (free_page && found == 0 && indx >= 0 && indx < MAXPAGES) {
      if (debug == 1) {
        fprintf(stderr, "Region  \"%s\" assumed as sea-level into page %d...\n", name, indx + 1);
        fflush(stderr);
      }

      dem[indx].max_west = maxlon;
      dem[indx].min_north = minlat;
      dem[indx].min_west = minlon;
      dem[indx].max_north = maxlat;

      /* Fill DEM with sea-level topography */

      for (x = 0; x < ippd; x++)
        for (y = 0; y < ippd; y++) {
          dem[indx].data[x][y] = 0;
          dem[indx].signal[x][y] = 0;
          dem[indx].mask[x][y] = 0;

          if (dem[indx].min_el > 0) dem[indx].min_el = 0;
        }

      if (dem[indx].min_el < min_elevation) min_elevation = dem[indx].min_el;

      if (dem[indx].max_el > max_elevation) max_elevation = dem[indx].max_el;

      if (max_north == -90)
        max_north = dem[indx].max_north;

      else if (dem[indx].max_north > max_north)
        max_north = dem[indx].max_north;

      if (min_north == 90)
        min_north = dem[indx].min_north;

      else if (dem[indx].min_north < min_north)
        min_north = dem[indx].min_north;

      if (max_west == -1)
        max_west = dem[indx].max_west;

      else {
        if (abs(dem[indx].max_west - max_west) < 180) {
          if (dem[indx].max_west > max_west) max_west = dem[indx].max_west;
        }

        else {
          if (dem[indx].max_west < max_west) max_west = dem[indx].max_west;
        }
      }

      if (min_west == 360)
        min_west = dem[indx].min_west;

      else {
        if (abs(dem[indx].min_west - min_west) < 180) {
          if (dem[indx].min_west < min_west) min_west = dem[indx].min_west;
        }

        else {
          if (dem[indx].min_west > min_west) min_west = dem[indx].min_west;
        }
      }

      return_value = 1;
    }
  }

  return return_value;
}

int LoadPAT(char *az_filename, char *el_filename)
{
  /* This function reads and processes antenna pattern (.az
     and .el) files that correspond in name to previously
     loaded ss .lrp files.  */

  int a, b, w, x, y, z, last_index, next_index, span;
  char string[255], *pointer = NULL;
  float az, xx, elevation, amplitude, rotation, valid1, valid2, delta, azimuth[361], azimuth_pattern[361], el_pattern[10001],
      elevation_pattern[361][1001], slant_angle[361], tilt, mechanical_tilt = 0.0, tilt_azimuth, tilt_increment, sum;
  FILE *fd = NULL;
  unsigned char read_count[10001];

  rotation = 0.0;

  got_azimuth_pattern = 0;
  got_elevation_pattern = 0;

  /* Load .az antenna pattern file */
  if (debug) {
    fprintf(stderr, "\nLoading antenna file \"%s\" ...", az_filename);
    fflush(stderr);
  }
  if (az_filename != NULL && (fd = fopen(az_filename, "r")) == NULL && errno != ENOENT) {
    /* Any error other than file not existing is an error */
    fprintf(stderr, "Error loading antena file %s, error number %d\n", az_filename, errno);
    return errno;
  }

  if (fd != NULL) {
    /* Clear azimuth pattern array */

    for (x = 0; x <= 360; x++) {
      azimuth[x] = 0.0;
      read_count[x] = 0;
    }

    /* Read azimuth pattern rotation
       in degrees measured clockwise
       from true North. */

    if (fgets(string, 254, fd) == NULL) {
      if (debug) {
        fprintf(stderr, "Azimuth read error \n");
      }
      // exit(0);
    }
    pointer = strchr(string, ';');

    if (pointer != NULL) *pointer = 0;

    sscanf(string, "%f", &rotation);

    /* Read azimuth (degrees) and corresponding
       normalized field radiation pattern amplitude
       (0.0 to 1.0) until EOF is reached. */

    if (fgets(string, 254, fd) == NULL) {
      // fprintf(stderr,"Azimuth read error\n");
      // exit(0);
    }
    pointer = strchr(string, ';');

    if (pointer != NULL) *pointer = 0;

    sscanf(string, "%f %f", &az, &amplitude);

    do {
      x = (int)rintf(az);

      if (x >= 0 && x <= 360 && fd != NULL) {
        azimuth[x] += amplitude;
        read_count[x]++;
      }

      if (fgets(string, 254, fd) == NULL) {
        // fprintf(stderr,"Azimuth read error\n");
        // exit(0);
      }
      pointer = strchr(string, ';');

      if (pointer != NULL) *pointer = 0;

      sscanf(string, "%f %f", &az, &amplitude);

    } while (feof(fd) == 0);

    fclose(fd);
    fd = NULL;

    /* Handle 0=360 degree ambiguity */

    if ((read_count[0] == 0) && (read_count[360] != 0)) {
      read_count[0] = read_count[360];
      azimuth[0] = azimuth[360];
    }

    if ((read_count[0] != 0) && (read_count[360] == 0)) {
      read_count[360] = read_count[0];
      azimuth[360] = azimuth[0];
    }

    /* Average pattern values in case more than
       one was read for each degree of azimuth. */

    for (x = 0; x <= 360; x++) {
      if (read_count[x] > 1) azimuth[x] /= (float)read_count[x];
    }

    /* Interpolate missing azimuths
       to completely fill the array */

    last_index = -1;
    next_index = -1;

    for (x = 0; x <= 360; x++) {
      if (read_count[x] != 0) {
        if (last_index == -1)
          last_index = x;
        else
          next_index = x;
      }

      if (last_index != -1 && next_index != -1) {
        valid1 = azimuth[last_index];
        valid2 = azimuth[next_index];

        span = next_index - last_index;
        delta = (valid2 - valid1) / (float)span;

        for (y = last_index + 1; y < next_index; y++) azimuth[y] = azimuth[y - 1] + delta;

        last_index = y;
        next_index = -1;
      }
    }

    /* Perform azimuth pattern rotation
       and load azimuth_pattern[361] with
       azimuth pattern data in its final form. */

    for (x = 0; x < 360; x++) {
      y = x + (int)rintf(rotation);

      if (y >= 360) y -= 360;

      azimuth_pattern[y] = azimuth[x];
    }

    azimuth_pattern[360] = azimuth_pattern[0];

    got_azimuth_pattern = 255;
  }

  /* Read and process .el file */

  if (el_filename != NULL && (fd = fopen(el_filename, "r")) == NULL && errno != ENOENT)
    /* Any error other than file not existing is an error */
    return errno;

  if (fd != NULL) {
    for (x = 0; x <= 10000; x++) {
      el_pattern[x] = 0.0;
      read_count[x] = 0;
    }

    /* Read mechanical tilt (degrees) and
       tilt azimuth in degrees measured
       clockwise from true North. */

    if (fgets(string, 254, fd) == NULL) {
      // fprintf(stderr,"Tilt read error\n");
      // exit(0);
    }
    pointer = strchr(string, ';');

    if (pointer != NULL) *pointer = 0;

    sscanf(string, "%f %f", &mechanical_tilt, &tilt_azimuth);

    /* Read elevation (degrees) and corresponding
       normalized field radiation pattern amplitude
       (0.0 to 1.0) until EOF is reached. */

    if (fgets(string, 254, fd) == NULL) {
      // fprintf(stderr,"Ant elevation read error\n");
      // exit(0);
    }
    pointer = strchr(string, ';');

    if (pointer != NULL) *pointer = 0;

    sscanf(string, "%f %f", &elevation, &amplitude);

    while (feof(fd) == 0) {
      /* Read in normalized radiated field values
         for every 0.01 degrees of elevation between
         -10.0 and +90.0 degrees */

      x = (int)rintf(100.0 * (elevation + 10.0));

      if (x >= 0 && x <= 10000) {
        el_pattern[x] += amplitude;
        read_count[x]++;
      }

      if (fgets(string, 254, fd) != NULL) {
        pointer = strchr(string, ';');
      }
      if (pointer != NULL) *pointer = 0;

      sscanf(string, "%f %f", &elevation, &amplitude);
    }

    fclose(fd);

    /* Average the field values in case more than
       one was read for each 0.01 degrees of elevation. */

    for (x = 0; x <= 10000; x++) {
      if (read_count[x] > 1) el_pattern[x] /= (float)read_count[x];
    }

    /* Interpolate between missing elevations (if
       any) to completely fill the array and provide
       radiated field values for every 0.01 degrees of
       elevation. */

    last_index = -1;
    next_index = -1;

    for (x = 0; x <= 10000; x++) {
      if (read_count[x] != 0) {
        if (last_index == -1)
          last_index = x;
        else
          next_index = x;
      }

      if (last_index != -1 && next_index != -1) {
        valid1 = el_pattern[last_index];
        valid2 = el_pattern[next_index];

        span = next_index - last_index;
        delta = (valid2 - valid1) / (float)span;

        for (y = last_index + 1; y < next_index; y++) el_pattern[y] = el_pattern[y - 1] + delta;

        last_index = y;
        next_index = -1;
      }
    }

    /* Fill slant_angle[] array with offset angles based
       on the antenna's mechanical beam tilt (if any)
       and tilt direction (azimuth). */

    if (mechanical_tilt == 0.0) {
      for (x = 0; x <= 360; x++) slant_angle[x] = 0.0;
    }

    else {
      tilt_increment = mechanical_tilt / 90.0;

      for (x = 0; x <= 360; x++) {
        xx = (float)x;
        y = (int)rintf(tilt_azimuth + xx);

        while (y >= 360) y -= 360;

        while (y < 0) y += 360;

        if (x <= 180) slant_angle[y] = -(tilt_increment * (90.0 - xx));

        if (x > 180) slant_angle[y] = -(tilt_increment * (xx - 270.0));
      }
    }

    slant_angle[360] = slant_angle[0]; /* 360 degree wrap-around */

    for (w = 0; w <= 360; w++) {
      tilt = slant_angle[w];

      /** Convert tilt angle to
              an array index offset **/

      y = (int)rintf(100.0 * tilt);

      /* Copy shifted el_pattern[10001] field
         values into elevation_pattern[361][1001]
         at the corresponding azimuth, downsampling
         (averaging) along the way in chunks of 10. */

      for (x = y, z = 0; z <= 1000; x += 10, z++) {
        for (sum = 0.0, a = 0; a < 10; a++) {
          b = a + x;

          if (b >= 0 && b <= 10000) sum += el_pattern[b];
          if (b < 0) sum += el_pattern[0];
          if (b > 10000) sum += el_pattern[10000];
        }

        elevation_pattern[w][z] = sum / 10.0;
      }
    }

    got_elevation_pattern = 255;

    for (x = 0; x <= 360; x++) {
      for (y = 0; y <= 1000; y++) {
        if (got_elevation_pattern)
          elevation = elevation_pattern[x][y];
        else
          elevation = 1.0;

        if (got_azimuth_pattern)
          az = azimuth_pattern[x];
        else
          az = 1.0;

        LR.antenna_pattern[x][y] = az * elevation;
      }
    }
  }
  return 0;
}

int LoadSignalColors(struct site xmtr)
{
  int x, y, ok, val[4];
  char filename[255], string[80], *pointer = NULL, *s = NULL;
  FILE *fd = NULL;

  for (x = 0; xmtr.filename[x] != '.' && xmtr.filename[x] != 0 && x < 250; x++) filename[x] = xmtr.filename[x];

  filename[x] = '.';
  filename[x + 1] = 's';
  filename[x + 2] = 'c';
  filename[x + 3] = 'f';
  filename[x + 4] = 0;

  /* Default values */

  region.level[0] = 128;
  region.color[0][0] = 255;
  region.color[0][1] = 0;
  region.color[0][2] = 0;

  region.level[1] = 118;
  region.color[1][0] = 255;
  region.color[1][1] = 165;
  region.color[1][2] = 0;

  region.level[2] = 108;
  region.color[2][0] = 255;
  region.color[2][1] = 206;
  region.color[2][2] = 0;

  region.level[3] = 98;
  region.color[3][0] = 255;
  region.color[3][1] = 255;
  region.color[3][2] = 0;

  region.level[4] = 88;
  region.color[4][0] = 184;
  region.color[4][1] = 255;
  region.color[4][2] = 0;

  region.level[5] = 78;
  region.color[5][0] = 0;
  region.color[5][1] = 255;
  region.color[5][2] = 0;

  region.level[6] = 68;
  region.color[6][0] = 0;
  region.color[6][1] = 208;
  region.color[6][2] = 0;

  region.level[7] = 58;
  region.color[7][0] = 0;
  region.color[7][1] = 196;
  region.color[7][2] = 196;

  region.level[8] = 48;
  region.color[8][0] = 0;
  region.color[8][1] = 148;
  region.color[8][2] = 255;

  region.level[9] = 38;
  region.color[9][0] = 80;
  region.color[9][1] = 80;
  region.color[9][2] = 255;

  region.level[10] = 28;
  region.color[10][0] = 0;
  region.color[10][1] = 38;
  region.color[10][2] = 255;

  region.level[11] = 18;
  region.color[11][0] = 142;
  region.color[11][1] = 63;
  region.color[11][2] = 255;

  region.level[12] = 8;
  region.color[12][0] = 140;
  region.color[12][1] = 0;
  region.color[12][2] = 128;

  region.levels = 13;

  /* Don't save if we don't have an output file */
  if ((fd = fopen(filename, "r")) == NULL && xmtr.filename[0] == '\0')
    // if ( xmtr.filename[0] == '\0' && (fd = fopen(filename, "r")) == NULL )
    return 0;

  if (fd == NULL) {
    if ((fd = fopen(filename, "w")) == NULL) return errno;

    for (x = 0; x < region.levels; x++)
      fprintf(fd, "%3d: %3d, %3d, %3d\n", region.level[x], region.color[x][0], region.color[x][1], region.color[x][2]);

    fclose(fd);
  }

  else {
    x = 0;
    s = fgets(string, 80, fd);

    while (x < 128 && feof(fd) == 0) {
      pointer = strchr(string, ';');

      if (pointer != NULL) *pointer = 0;

      ok = sscanf(string, "%d: %d, %d, %d", &val[0], &val[1], &val[2], &val[3]);

      if (ok == 4) {
        if (debug) {
          fprintf(stderr, "\nLoadSignalColors() %d: %d, %d, %d\n", val[0], val[1], val[2], val[3]);
          fflush(stderr);
        }

        for (y = 0; y < 4; y++) {
          if (val[y] > 255) val[y] = 255;

          if (val[y] < 0) val[y] = 0;
        }

        region.level[x] = val[0];
        region.color[x][0] = val[1];
        region.color[x][1] = val[2];
        region.color[x][2] = val[3];
        x++;
      }

      s = fgets(string, 80, fd);
    }

    fclose(fd);
    region.levels = x;
  }
  return 0;
}

int LoadLossColors(struct site xmtr)
{
  int x, y, ok, val[4];
  char filename[255], string[80], *pointer = NULL, *s = NULL;
  FILE *fd = NULL;

  for (x = 0; xmtr.filename[x] != '.' && xmtr.filename[x] != 0 && x < 250; x++) filename[x] = xmtr.filename[x];

  filename[x] = '.';
  filename[x + 1] = 'l';
  filename[x + 2] = 'c';
  filename[x + 3] = 'f';
  filename[x + 4] = 0;

  /* Default values */

  region.level[0] = 80;
  region.color[0][0] = 255;
  region.color[0][1] = 0;
  region.color[0][2] = 0;

  region.level[1] = 90;
  region.color[1][0] = 255;
  region.color[1][1] = 128;
  region.color[1][2] = 0;

  region.level[2] = 100;
  region.color[2][0] = 255;
  region.color[2][1] = 165;
  region.color[2][2] = 0;

  region.level[3] = 110;
  region.color[3][0] = 255;
  region.color[3][1] = 206;
  region.color[3][2] = 0;

  region.level[4] = 120;
  region.color[4][0] = 255;
  region.color[4][1] = 255;
  region.color[4][2] = 0;

  region.level[5] = 130;
  region.color[5][0] = 184;
  region.color[5][1] = 255;
  region.color[5][2] = 0;

  region.level[6] = 140;
  region.color[6][0] = 0;
  region.color[6][1] = 255;
  region.color[6][2] = 0;

  region.level[7] = 150;
  region.color[7][0] = 0;
  region.color[7][1] = 208;
  region.color[7][2] = 0;

  region.level[8] = 160;
  region.color[8][0] = 0;
  region.color[8][1] = 196;
  region.color[8][2] = 196;

  region.level[9] = 170;
  region.color[9][0] = 0;
  region.color[9][1] = 148;
  region.color[9][2] = 255;

  region.level[10] = 180;
  region.color[10][0] = 80;
  region.color[10][1] = 80;
  region.color[10][2] = 255;

  region.level[11] = 190;
  region.color[11][0] = 0;
  region.color[11][1] = 38;
  region.color[11][2] = 255;

  region.level[12] = 200;
  region.color[12][0] = 142;
  region.color[12][1] = 63;
  region.color[12][2] = 255;

  region.level[13] = 210;
  region.color[13][0] = 196;
  region.color[13][1] = 54;
  region.color[13][2] = 255;

  region.level[14] = 220;
  region.color[14][0] = 255;
  region.color[14][1] = 0;
  region.color[14][2] = 255;

  region.level[15] = 230;
  region.color[15][0] = 255;
  region.color[15][1] = 194;
  region.color[15][2] = 204;

  region.levels = 16;

  if ((fd = fopen(filename, "r")) == NULL && xmtr.filename[0] == '\0')
    // if ( xmtr.filename[0] == '\0' && (fd = fopen(filename, "r")) == NULL )
    /* Don't save if we don't have an output file */
    return 0;

  if (fd == NULL) {
    if ((fd = fopen(filename, "w")) == NULL) return errno;

    for (x = 0; x < region.levels; x++)
      fprintf(fd, "%3d: %3d, %3d, %3d\n", region.level[x], region.color[x][0], region.color[x][1], region.color[x][2]);

    fclose(fd);

    if (debug) {
      fprintf(stderr, "loadLossColors: fopen fail: %s\n", filename);
      fflush(stderr);
    }
  }

  else {
    x = 0;
    s = fgets(string, 80, fd);

    while (x < 128 && feof(fd) == 0) {
      pointer = strchr(string, ';');

      if (pointer != NULL) *pointer = 0;

      ok = sscanf(string, "%d: %d, %d, %d", &val[0], &val[1], &val[2], &val[3]);

      if (ok == 4) {
        if (debug) {
          fprintf(stderr, "\nLoadLossColors() %d: %d, %d, %d\n", val[0], val[1], val[2], val[3]);
          fflush(stderr);
        }

        for (y = 0; y < 4; y++) {
          if (val[y] > 255) val[y] = 255;

          if (val[y] < 0) val[y] = 0;
        }

        region.level[x] = val[0];
        region.color[x][0] = val[1];
        region.color[x][1] = val[2];
        region.color[x][2] = val[3];
        x++;
      }

      s = fgets(string, 80, fd);
    }

    fclose(fd);
    region.levels = x;
  }
  return 0;
}

int LoadDBMColors(struct site xmtr, double rxGain)
{
  int x, y, ok, val[4];
  char filename[255], string[80], *pointer = NULL, *s = NULL;
  FILE *fd = NULL;

  for (x = 0; xmtr.filename[x] != '.' && xmtr.filename[x] != 0 && x < 250; x++) filename[x] = xmtr.filename[x];

  filename[x] = '.';
  filename[x + 1] = 'd';
  filename[x + 2] = 'c';
  filename[x + 3] = 'f';
  filename[x + 4] = 0;

  /* Default values */

  region.level[0] = 0;
  region.color[0][0] = 255;
  region.color[0][1] = 0;
  region.color[0][2] = 0;

  region.level[1] = -10;
  region.color[1][0] = 255;
  region.color[1][1] = 128;
  region.color[1][2] = 0;

  region.level[2] = -20;
  region.color[2][0] = 255;
  region.color[2][1] = 165;
  region.color[2][2] = 0;

  region.level[3] = -30;
  region.color[3][0] = 255;
  region.color[3][1] = 206;
  region.color[3][2] = 0;

  region.level[4] = -40;
  region.color[4][0] = 255;
  region.color[4][1] = 255;
  region.color[4][2] = 0;

  region.level[5] = -50;
  region.color[5][0] = 184;
  region.color[5][1] = 255;
  region.color[5][2] = 0;

  region.level[6] = -60;
  region.color[6][0] = 0;
  region.color[6][1] = 255;
  region.color[6][2] = 0;

  region.level[7] = -70;
  region.color[7][0] = 0;
  region.color[7][1] = 208;
  region.color[7][2] = 0;

  region.level[8] = -80;
  region.color[8][0] = 0;
  region.color[8][1] = 196;
  region.color[8][2] = 196;

  region.level[9] = -90;
  region.color[9][0] = 0;
  region.color[9][1] = 148;
  region.color[9][2] = 255;

  region.level[10] = -100;
  region.color[10][0] = 80;
  region.color[10][1] = 80;
  region.color[10][2] = 255;

  region.level[11] = -110;
  region.color[11][0] = 0;
  region.color[11][1] = 38;
  region.color[11][2] = 255;

  region.level[12] = -120;
  region.color[12][0] = 142;
  region.color[12][1] = 63;
  region.color[12][2] = 255;

  region.level[13] = -130;
  region.color[13][0] = 196;
  region.color[13][1] = 54;
  region.color[13][2] = 255;

  region.level[14] = -140;
  region.color[14][0] = 255;
  region.color[14][1] = 0;
  region.color[14][2] = 255;

  region.level[15] = -150;
  region.color[15][0] = 255;
  region.color[15][1] = 194;
  region.color[15][2] = 204;

  region.levels = 16;

  if ((fd = fopen(filename, "r")) == NULL && xmtr.filename[0] == '\0') /* Don't save if we don't have an output file */
    return 0;

  if (fd == NULL) {
    if ((fd = fopen(filename, "w")) == NULL) return errno;

    for (x = 0; x < region.levels; x++)
      fprintf(fd, "%+4d: %3d, %3d, %3d\n", region.level[x], region.color[x][0], region.color[x][1], region.color[x][2]);

    fclose(fd);
  }

  else {
    x = 0;
    s = fgets(string, 80, fd);

    while (x < 128 && feof(fd) == 0) {
      pointer = strchr(string, ';');

      if (pointer != NULL) *pointer = 0;

      ok = sscanf(string, "%d: %d, %d, %d", &val[0], &val[1], &val[2], &val[3]);

      if (ok == 4) {
        val[0] = val[0] - rxGain;

        if (debug) {
          fprintf(stderr, "\nLoadDBMColors() %d: %d, %d, %d\n", val[0], val[1], val[2], val[3]);
          fflush(stderr);
        }

        if (val[0] < -200) val[0] = -200;

        if (val[0] > +40) val[0] = +40;

        region.level[x] = val[0];

        for (y = 1; y < 4; y++) {
          if (val[y] > 255) val[y] = 255;

          if (val[y] < 0) val[y] = 0;
        }

        region.color[x][0] = val[1];
        region.color[x][1] = val[2];
        region.color[x][2] = val[3];
        x++;
      }

      s = fgets(string, 80, fd);
    }

    fclose(fd);
    region.levels = x;
  }
  return 0;
}

int LoadTopoData(int max_lon, int min_lon, int max_lat, int min_lat)
{
  /* This function loads the SDF files required
     to cover the limits of the region specified. */

  int x, y, width, ymin, ymax;
  int success;

  width = ReduceAngle(max_lon - min_lon);

  if ((max_lon - min_lon) <= 180.0) {
    for (y = 0; y <= width; y++)
      for (x = min_lat; x <= max_lat; x++) {
        ymin = (int)(min_lon + (double)y);

        while (ymin < 0) ymin += 360;

        while (ymin >= 360) ymin -= 360;

        ymax = ymin + 1;

        while (ymax < 0) ymax += 360;

        while (ymax >= 360) ymax -= 360;

        if (ippd == 3600)
          snprintf(string, 19, "%d:%d:%d:%d-hd", x, x + 1, ymin, ymax);
        else
          snprintf(string, 16, "%d:%d:%d:%d", x, x + 1, ymin, ymax);
        if ((success = LoadSDF(string)) < 0) {
          return -success;
        }
      }
  }

  else {
    for (y = 0; y <= width; y++)
      for (x = min_lat; x <= max_lat; x++) {
        ymin = max_lon + y;

        while (ymin < 0) ymin += 360;

        while (ymin >= 360) ymin -= 360;

        ymax = ymin + 1;

        while (ymax < 0) ymax += 360;

        while (ymax >= 360) ymax -= 360;

        if (ippd == 3600)
          snprintf(string, 19, "%d:%d:%d:%d-hd", x, x + 1, ymin, ymax);
        else
          snprintf(string, 16, "%d:%d:%d:%d", x, x + 1, ymin, ymax);
        if ((success = LoadSDF(string)) < 0) {
          return -success;
        }
      }
  }
  return 0;
}

int LoadUDT(char *filename)
{
  /* This function reads a file containing User-Defined Terrain
     features for their addition to the digital elevation model
     data used by SPLAT!.  Elevations in the UDT file are evaluated
     and then copied into a temporary file under /tmp.  Then the
     contents of the temp file are scanned, and if found to be unique,
     are added to the ground elevations described by the digital
     elevation data already loaded into memory. */

  int i, x, y, z, ypix, xpix, tempxpix, tempypix, fd = 0, n = 0;
  char input[80], str[3][80], tempname[15], *pointer = NULL, *s = NULL;
  double latitude, longitude, height, tempheight;
  FILE *fd1 = NULL, *fd2 = NULL;

  strcpy(tempname, "/tmp/XXXXXX");

  if ((fd1 = fopen(filename, "r")) == NULL) return errno;

  if ((fd = mkstemp(tempname)) == -1) return errno;

  if ((fd2 = fdopen(fd, "w")) == NULL) {
    fclose(fd1);
    close(fd);
    return errno;
  }

  s = fgets(input, 78, fd1);

  pointer = strchr(input, ';');

  if (pointer != NULL) *pointer = 0;

  while (feof(fd1) == 0) {
    /* Parse line for latitude, longitude, height */

    for (x = 0, y = 0, z = 0; x < 78 && input[x] != 0 && z < 3; x++) {
      if (input[x] != ',' && y < 78) {
        str[z][y] = input[x];
        y++;
      }

      else {
        str[z][y] = 0;
        z++;
        y = 0;
      }
    }

    latitude = ReadBearing(str[0]);
    longitude = ReadBearing(str[1]);

    if (longitude < 0.0) longitude += 360;

    /* Remove <CR> and/or <LF> from antenna height string */

    for (i = 0; str[2][i] != 13 && str[2][i] != 10 && str[2][i] != 0; i++)
      ;

    str[2][i] = 0;

    /* The terrain feature may be expressed in either
       feet or meters.  If the letter 'M' or 'm' is
       discovered in the string, then this is an
       indication that the value given is expressed
       in meters.  Otherwise the height is interpreted
       as being expressed in feet.  */

    for (i = 0; str[2][i] != 'M' && str[2][i] != 'm' && str[2][i] != 0 && i < 48; i++)
      ;

    if (str[2][i] == 'M' || str[2][i] == 'm') {
      str[2][i] = 0;
      height = rint(atof(str[2]));
    }

    else {
      str[2][i] = 0;
      height = rint(METERS_PER_FOOT * atof(str[2]));
    }

    if (height > 0.0) fprintf(fd2, "%d, %d, %f\n", (int)rint(latitude / dpp), (int)rint(longitude / dpp), height);

    s = fgets(input, 78, fd1);

    pointer = strchr(input, ';');

    if (pointer != NULL) *pointer = 0;
  }

  fclose(fd1);
  fclose(fd2);

  if ((fd1 = fopen(tempname, "r")) == NULL) return errno;

  if ((fd2 = fopen(tempname, "r")) == NULL) {
    fclose(fd1);
    return errno;
  }

  y = 0;

  n = fscanf(fd1, "%d, %d, %lf", &xpix, &ypix, &height);

  do {
    x = 0;
    z = 0;

    n = fscanf(fd2, "%d, %d, %lf", &tempxpix, &tempypix, &tempheight);

    do {
      if (x > y && xpix == tempxpix && ypix == tempypix) {
        z = 1; /* Dupe! */

        if (tempheight > height) height = tempheight;
      }

      else {
        n = fscanf(fd2, "%d, %d, %lf", &tempxpix, &tempypix, &tempheight);
        x++;
      }

    } while (feof(fd2) == 0 && z == 0);

    if (z == 0) /* No duplicate found */
      // fprintf(stderr,"%lf, %lf \n",xpix*dpp, ypix*dpp);
      fflush(stderr);
    AddElevation(xpix * dpp, ypix * dpp, height, 1);
    fflush(stderr);

    n = fscanf(fd1, "%d, %d, %lf", &xpix, &ypix, &height);
    y++;

    rewind(fd2);

  } while (feof(fd1) == 0);

  fclose(fd1);
  fclose(fd2);
  unlink(tempname);
  return 0;
}
