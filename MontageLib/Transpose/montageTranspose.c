/* Module: mTranspose.c

Version  Developer        Date     Change
-------  ---------------  -------  -----------------------
1.3      John Good        08Sep15  fits_read_pix() incorrect null value
1.2      John Good        02Sep15  Warning cleanup
1.1      John Good        27Jul15  Test results updates
1.0      John Good        30Jul14  Baseline code

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include <fitsio.h>
#include <wcs.h>
#include <coord.h>

#include <mTranspose.h>
#include <montage.h>

#define STRLEN  1024


/***************************/
/* Define global variables */
/***************************/

static int At[4][4];
static int Bt[4];

static int norder;
static int order  [4];
static int reorder[4];

static char montage_msgstr[1024];
static char montage_json  [1024];


/*-***********************************************************************/
/*                                                                       */
/*  mTranspose                                                           */
/*                                                                       */
/*  This module switches around the axes for a 3D or 4D image.  This is  */
/*  mainly so we can get a cube rearranged so the spatial axes are the   */
/*  first two.                                                           */
/*                                                                       */
/*  This program subsets an input image around a location of interest    */
/*  and creates a new output image consisting of just those pixels.      */
/*  The location is defined by the RA,Dec (J2000) of the new center and  */
/*  the XY size in degrees of the area (X and Y) in the direction of     */
/*  the image axes, not Equatorial coordinates.                          */
/*                                                                       */
/*   char  *inputFile      Input FITS file                               */
/*   char  *outputFile     Subimage output FITS file                     */
/*                                                                       */
/*   int    norder         Number of axes (3 or 4)                       */
/*                                                                       */
/*   int    order          The output ordering of axes desired.  For     */
/*                         instance, to convert a cube where the first   */
/*                         axis is wavelength, the second RA and the     */
/*                         third Dec to RA, Dec, wavelength ordering     */
/*                         (a common situation), the output order is     */
/*                         2,3,1.                                        */
/*                                                                       */
/*   int    debug          Debugging output level                        */
/*                                                                       */
/*************************************************************************/

struct mTransposeReturn *mTranspose(char *inputFile, char *outputFile, int innorder, int *inorder, int debug)
{
   int        i, j, k, l;
   int        it, jt, kt, lt;
   int        bitpix, datatype, first;
   int        nullcnt, status, nfound, keynum;
   long       fpixel[4];

   long       naxis;
   long       nAxisIn [4];
   long       nAxisOut[4];

   double    *indata;
   double ****outdata;

   char       card      [STRLEN];
   char       newcard   [STRLEN];
   char       keyname   [STRLEN];
   char       value     [STRLEN];
   char       comment   [STRLEN];
   char       errstr    [STRLEN];

   double     mindata, maxdata;

   fitsfile  *inFptr;
   fitsfile  *outFptr;

   struct mTransposeReturn *returnStruct;

   norder = innorder;

   for(i=0; i<4; ++i)
      order[i] = -1;

   for(i=0; i<norder; ++i)
      order[i] = inorder[i];


   /************************************************/
   /* Make a NaN value to use setting blank pixels */
   /************************************************/

   union
   {
      double d;
      char   c[8];
   }
   nvalue;

   double nan;

   for(i=0; i<8; ++i)
      nvalue.c[i] = 255;

   nan = nvalue.d;


   /*******************************/
   /* Initialize return structure */
   /*******************************/

   returnStruct = (struct mTransposeReturn *)malloc(sizeof(struct mTransposeReturn));

   bzero((void *)returnStruct, sizeof(returnStruct));


   returnStruct->status = 1;

   strcpy(returnStruct->msg, "");


   /************************/
   /* Open the input image */
   /************************/

   status = 0;
   if(fits_open_file(&inFptr, inputFile, READONLY, &status))
   {
      sprintf(errstr, "Input image file %s missing or invalid FITS", inputFile);
      mTranspose_printError(errstr);
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }

   if(mTranspose_analyzeCTYPE(inFptr) > 0)
   {
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }

   status = 0;
   if(fits_get_img_type(inFptr, &bitpix, &status))
   {
      mTranspose_printFitsError(status);
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }

        if(bitpix ==   8) datatype = TBYTE;
   else if(bitpix ==  16) datatype = TSHORT;
   else if(bitpix ==  32) datatype = TLONG;
   else if(bitpix ==  64) datatype = TLONGLONG;
   else if(bitpix == -32) datatype = TFLOAT;
   else if(bitpix == -64) datatype = TDOUBLE;

   status = 0;
   if(fits_read_keys_lng(inFptr, "NAXIS", 1, 4, nAxisIn, &nfound, &status))
   {
      mTranspose_printFitsError(status);
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }

   if(nfound < 4)
      nAxisIn[3] = 1;

   if(nfound < 3)
      nAxisIn[2] = 1;

   naxis = nfound;

   if(debug)
   {
      printf("naxis       =  %ld\n",   naxis);
      printf("nAxisIn[0]  =  %ld\n",   nAxisIn[0]);
      printf("nAxisIn[1]  =  %ld\n",   nAxisIn[1]);
      printf("nAxisIn[2]  =  %ld\n",   nAxisIn[2]);
      printf("nAxisIn[3]  =  %ld\n",   nAxisIn[3]);
      printf("\n");
      fflush(stdout);
   }


   /****************************************************/
   /* We analyzed the image for lat/lot and initialize */
   /* the transpose order.  Here we optionally set it  */
   /* manually with command-line arguments.            */
   /****************************************************/

   if(norder != naxis)
   {
      sprintf(returnStruct->msg, "Image has %ld dimensions.  You must list the output order for all of them.", naxis);
      return returnStruct;
   }

   for(i=0; i<norder; ++i)
   {
      if(order[i] < 1 || order[i] > naxis)
      {
         sprintf(returnStruct->msg, "Axis ID %d must be between 1 and %ld.", i+1, naxis);
         return returnStruct;
      }
   }

   for(i=0; i<norder; ++i)
   {
      for(j=0; j<i; ++j)
      {
         if(order[j] == order[i])
         {
            sprintf(returnStruct->msg, "Output axis %d is the same as axis %d. They must be unique.", i+1, j+1);
            return returnStruct;
         }
      }
   }

   if(debug >= 1)
   {
      printf("\n");
      printf("debug       = %d\n",   debug);
      printf("\n");
      printf("inputFile   = [%s]\n", inputFile);
      printf("outputFile  = [%s]\n", outputFile);
      printf("\n");

      for(i=0; i<norder; ++i)
         printf("order[%d]    = [%d]\n", i, order[i]);

      printf("\n");

      fflush(stdout);
   }


   /***********************************/
   /* Initialize the transform matrix */
   /***********************************/

   mTranspose_initTransform(nAxisIn, nAxisOut);

   if(debug)
   {
      printf("nAxisOut[0] =  %ld\n", nAxisOut[0]);
      printf("nAxisOut[1] =  %ld\n", nAxisOut[1]);
      printf("nAxisOut[2] =  %ld\n", nAxisOut[2]);
      printf("nAxisOut[3] =  %ld\n", nAxisOut[3]);
      printf("\n");

      printf("*it = %d*i + %d*j + %d*k + %d*l + %d\n", At[0][0], At[0][1], At[0][2], At[0][3], Bt[0]);
      printf("*jt = %d*i + %d*j + %d*k + %d*l + %d\n", At[1][0], At[1][1], At[1][2], At[0][3], Bt[1]);
      printf("*kt = %d*i + %d*j + %d*k + %d*l + %d\n", At[2][0], At[2][1], At[2][2], At[0][3], Bt[2]);
      printf("*lt = %d*i + %d*j + %d*k + %d*l + %d\n", At[3][0], At[3][1], At[3][2], At[0][3], Bt[3]);
      printf("\n");

      printf("reorder[0]  =  %d\n", reorder[0]);
      printf("reorder[1]  =  %d\n", reorder[1]);
      printf("reorder[2]  =  %d\n", reorder[2]);
      printf("reorder[3]  =  %d\n", reorder[3]);
      printf("\n");
      fflush(stdout);
   }


   /**********************************************/
   /* Allocate memory for the input image pixels */
   /* We don't need the whole array, just one    */
   /* line which we read in and immediately      */
   /* redistribute to the output array.          */
   /**********************************************/

   indata = (double *)malloc(nAxisIn[0] * sizeof(double));


   /***********************************************/
   /* Allocate memory for the output image pixels */
   /***********************************************/

   outdata = (double ****)malloc(nAxisOut[3] * sizeof(double ***));

   for(l=0; l<nAxisOut[3]; ++l)
   {
      if(debug && l == 0)
      {
         printf("%ld (double **) allocated %ld times\n", nAxisOut[2], nAxisOut[3]);
         fflush(stdout);
      }

      outdata[l] = (double ***)malloc(nAxisOut[2] * sizeof(double **));

      for(k=0; k<nAxisOut[2]; ++k)
      {
         if(debug && l == 0 && k == 0)
         {
            printf("%ld (double *) allocated %ldx%ld times\n", nAxisOut[1], nAxisOut[2], nAxisOut[3]);
            fflush(stdout);
         }

         outdata[l][k] = (double **)malloc(nAxisOut[1] * sizeof(double *));

         for(j=0; j<nAxisOut[1]; ++j)
         {
            if(debug && l == 0 && k == 0 && j == 0)
            {
               printf("%ld (double) allocated %ldx%ldx%ld times\n", nAxisOut[0], nAxisOut[1], nAxisOut[2], nAxisOut[3]);
               fflush(stdout);
            }

            outdata[l][k][j] = (double *)malloc(nAxisOut[0] * sizeof(double));

            for(i=0; i<nAxisOut[0]; ++i)
            {
               if(debug && l == 0 && k == 0 && j == 0 && i == 0)
               {
                  printf("%ld doubles zeroed %ldx%ldx%ld times\n\n", nAxisOut[0], nAxisOut[1], nAxisOut[2], nAxisOut[3]);
                  fflush(stdout);
               }

               outdata[l][k][j][i] = 0;
            }
         }
      }
   }

   if(debug >= 1)
   {
      printf("%ld bytes allocated for input image pixels\n\n",
         nAxisOut[0] * nAxisOut[1] * nAxisOut[2] * nAxisOut[3] * sizeof(double));
      fflush(stdout);
   }


   /*************************/
   /* Read / write the data */
   /*************************/

   fpixel[0] = 1;

   status = 0;
   first  = 1;

   for (l=0; l<nAxisIn[3]; ++l)
   {
      // For each 4D plane

      fpixel[3] = l+1;

      for (k=0; k<nAxisIn[2]; ++k)
      {
         // For each plane

         fpixel[2] = k+1;

         for (j=0; j<nAxisIn[1]; ++j)
         {
            // Read lines from the input file

            fpixel[1] = j+1;

            if(debug >= 2)
            {
               printf("Reading input 4plane/plane/row %5d/%5d/%5d\n", l, k, j);
               fflush(stdout);
            }

            status = 0;

            if(fits_read_pix(inFptr, TDOUBLE, fpixel, nAxisIn[0], &nan,
                             (void *)indata, &nullcnt, &status))
            {
               mTranspose_printFitsError(status);
               strcpy(returnStruct->msg, montage_msgstr);
               return returnStruct;
            }


            // And copy the data to the correct
            // pixels in the output array

            if(debug >= 3)
            {
               printf("\n");
               printf("%5s %5s %5s %5s -> %5s %5s %5s %5s\n", "l", "k", "j", "i", "lt", "kt", "jt", "it");
               fflush(stdout);
            }

            for(i=0; i<nAxisIn[0]; ++i)
            {
               mTranspose_transform(i, j, k, l, &it, &jt, &kt, &lt);

               if(debug >= 3)
               {
                  printf("%5d %5d %5d %5d -> %5d %5d %5d %5d [%-g]\n",
                     l, k, j, i, lt, kt, jt, it, indata[i]);
                  fflush(stdout);
               }

               if(mNaN(indata[i]))
                  outdata[lt][kt][jt][it] = nan;
               else
               {
                  outdata[lt][kt][jt][it] = indata[i];

                  if(first)
                  {
                     mindata = indata[i];
                     maxdata = indata[i];
                     first = 0;
                  }
                  else
                  {
                     if(indata[i] < mindata) mindata = indata[i];
                     if(indata[i] > maxdata) maxdata = indata[i];
                  }
               }
            }
         }
      }
   }

   if(debug >= 1)
   {
      printf("Input image read complete.\n\n");
      fflush(stdout);
   }


   /********************************/
   /* Create the output FITS files */
   /********************************/

   remove(outputFile);

   if(fits_create_file(&outFptr, outputFile, &status))
   {
      mTranspose_printFitsError(status);
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }

   if(debug >= 1)
   {
      printf("\nFITS output files created (not yet populated)\n");
      fflush(stdout);
   }

   if (fits_create_img(outFptr, bitpix, naxis, nAxisOut, &status))
      {
         mTranspose_printFitsError(status);
         strcpy(returnStruct->msg, montage_msgstr);
         return returnStruct;
      }


   /**************************************************************/
   /* Copy all the header keywords from the input to the output. */
   /* Update the ones that change because of axes swapping and   */
   /* ignore BITPIX/NAXIS values.                                */
   /**************************************************************/

   keynum = 1;

   while(1)
   {
      status = 0;
      fits_read_record(inFptr, keynum, card, &status);

      status = 0;
      fits_read_keyn(inFptr, keynum, keyname, value, comment, &status);

      if(status)
         break;

      strcpy(newcard, mTranspose_checkKeyword(keyname, card, naxis));

      if(strlen(newcard) > 0)
      {
         if(debug >= 1)
         {
            printf("Header keyword %d: [%s][%s][%s]\n", keynum, keyname, value, comment);
            printf("               --> [%s]\n", newcard);
            fflush(stdout);
         }

         status = 0;
         if(fits_write_record(outFptr, newcard, &status))
         {
            sprintf(errstr, "Error writing card %d.", keynum);
            mTranspose_printError(errstr);
            strcpy(returnStruct->msg, montage_msgstr);
            return returnStruct;
         }
      }

      ++keynum;
   }


   // Now we switch the lower case keywords back to upper case

   keynum = 1;

   while(1)
   {
      status = 0;
      fits_read_keyn(outFptr, keynum, keyname, value, comment, &status);

      if(status)
         break;

      // printf("Header keyword %d: [%s][%s][%s]\n", keynum, keyname, value, comment);

      ++keynum;
   }

   if(debug >= 1)
   {
      printf("Header keywords copied to FITS output file with axes modifications\n");
      fflush(stdout);
   }

   status = 0;
   if(fits_close_file(inFptr, &status))
   {
      mTranspose_printFitsError(status);
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }


   /************************/
   /* Write the image data */
   /************************/

   fpixel[0] = 1;

   status = 0;

   for (l=0; l<nAxisOut[3]; ++l)
   {
      // For each 4D plane

      fpixel[3] = l+1;

      for (k=0; k<nAxisOut[2]; ++k)
      {
         // For each plane

         fpixel[2] = k+1;

         for (j=0; j<nAxisOut[1]; ++j)
         {
            // Write lines to the output file

            fpixel[1] = j+1;

            status = 0;

            if (fits_write_pix(outFptr, datatype, fpixel, nAxisOut[0],
                               (void *)outdata[l][k][j], &status))
            {
               mTranspose_printFitsError(status);
               strcpy(returnStruct->msg, montage_msgstr);
               return returnStruct;
            }

         }
      }
   }

   if(debug >= 1)
   {
      printf("Data written to FITS data image\n");
      fflush(stdout);
   }


   /******************************/
   /* Close the output FITS file */
   /******************************/

   if(fits_close_file(outFptr, &status))
   {
      mTranspose_printFitsError(status);
      strcpy(returnStruct->msg, montage_msgstr);
      return returnStruct;
   }

   if(debug >= 1)
   {
      printf("FITS data image finalized\n");
      fflush(stdout);
   }

   sprintf(montage_msgstr, "mindata=%-g, maxdata=%-g", mindata, maxdata);
   sprintf(montage_json, "{\"mindata\":%-g, \"maxdata\":%-g}", mindata, maxdata);

   returnStruct->status = 0;

   strcpy(returnStruct->msg,  montage_msgstr);
   strcpy(returnStruct->json, montage_json);

   returnStruct->mindata = mindata;
   returnStruct->maxdata = maxdata;

   return returnStruct;
}



/*******************************************/
/*                                         */
/* Analyze the CTYPE keywords to determine */
/* the default reordering.                 */
/*                                         */
/*******************************************/

int mTranspose_analyzeCTYPE(fitsfile *inFptr)
{
   int  i, status, lonaxis, lataxis;

   char ctype[4][16];

   status = 0;
   fits_read_key(inFptr, TSTRING, "CTYPE1", ctype[0], (char *)NULL, &status);

   if(status) strcpy(ctype[0], "NONE");

   status = 0;
   fits_read_key(inFptr, TSTRING, "CTYPE2", ctype[1], (char *)NULL, &status);

   if(status) strcpy(ctype[1], "NONE");

   status = 0;
   fits_read_key(inFptr, TSTRING, "CTYPE3", ctype[2], (char *)NULL, &status);

   if(status) strcpy(ctype[2], "NONE");

   status = 0;
   fits_read_key(inFptr, TSTRING, "CTYPE4", ctype[3], (char *)NULL, &status);

   if(status) strcpy(ctype[3], "NONE");


   for(i=0; i<4; ++i)
      order[i] = -1;

   lonaxis = -1;
   lataxis = -1;

   for(i=0; i<4; ++i)
   {
      if(strncmp(ctype[i], "RA--", 4) == 0
      || strncmp(ctype[i], "GLON", 4) == 0
      || strncmp(ctype[i], "ELON", 4) == 0
      || strncmp(ctype[i], "LON-", 4) == 0)
      {
         if(lonaxis == -1)
            lonaxis = i;
         else
         {
            mTranspose_printError("Multiple 'longitude' axes.");
            return 1;
         }
      }

      if(strncmp(ctype[i], "DEC-", 4) == 0
      || strncmp(ctype[i], "GLAT", 4) == 0
      || strncmp(ctype[i], "ELAT", 4) == 0
      || strncmp(ctype[i], "LAT-", 4) == 0)
      {
         if(lataxis == -1)
            lataxis = i;
         else
         {
            mTranspose_printError("Multiple 'latitude' axes.");
            return 1;
         }
      }
   }

   if(lonaxis == -1 || lataxis == -1)
   {
      mTranspose_printError("Need both longitude and latitude axes.");
      return 1;
   }

   order[0] = lonaxis;
   order[1] = lataxis;

   for(i=0; i<4; ++i)
   {
      if(i != lonaxis && i != lataxis)
      {
         if(order[2] == -1)
            order[2] = i;
         else
            order[3] = i;
      }
   }

   for(i=0; i<4; ++i)
      ++order[i];

   return 0;
}


/*****************************************************/
/*                                                   */
/*  Check which keywords we know need to be switched */
/*                                                   */
/*****************************************************/

static char *wcs[9] = { "NAXISn", "CRVALn", "CRPIXn",
                        "CTYPEn", "CDELTn", "CDn_n",
                        "CROTAn", "CUNITn", "PCn_n" };
static int nwcs = 9;


char *mTranspose_checkKeyword(char *keyname, char *card, long naxis)
{
   int i, j, match, index, newindex;

   static char retstr[STRLEN];

   char wcskey[STRLEN];

   // The axes counts and BITPIX were handle
   // when we created the image, so skip these

   if(strcmp(keyname, "SIMPLE") == 0
   || strcmp(keyname, "NAXIS" ) == 0
   || strcmp(keyname, "NAXIS1") == 0
   || strcmp(keyname, "NAXIS2") == 0
   || strcmp(keyname, "NAXIS3") == 0
   || strcmp(keyname, "NAXIS4") == 0
   || strcmp(keyname, "BITPIX") == 0)
   {
      strcpy(retstr, "");
      return retstr;
   }


   // Check whether this is one of the keywords
   // we know need to be transformed

   for(i=0; i<nwcs; ++i)
   {
      // NOTE: Its a bit of a kludge but in the case where
      // naxis=2 it is safer to not switch CROTA2 to CROTA1.
      // CROTA2 has become a standard shorthand for the overall
      // image rotation; a lot of software probably never thinks
      // to check CROTA1

      if(strncmp(wcs[i], "CROTA", 5) && naxis == 2)
         continue;

      // Check keyword for match and convert as
      // we go.  Abandon if bad match.

      strcpy(retstr, card);

      strcpy(wcskey, wcs[i]);

      if(strlen(keyname) != strlen(wcskey))
         continue;

      match = 1;

      for(j=0; j<strlen(keyname); ++j)
      {
         if(wcskey[j] == 'n')
         {
            index = keyname[j] - '1';

            newindex = reorder[index];

            retstr[j] = '1' + newindex;
         }
         else if(keyname[j] != wcskey[j])
         {
            match = 0;
            break;
         }
      }

      if(match)
         return retstr;
   }

   strcpy(retstr, card);
   return retstr;
}



/******************************/
/*                            */
/*  Print out general errors  */
/*                            */
/******************************/

void mTranspose_printError(char *msg)
{
   strcpy(montage_msgstr, msg);
}




/***********************************/
/*                                 */
/*  Print out FITS library errors  */
/*                                 */
/***********************************/

void mTranspose_printFitsError(int status)
{
   char status_str[FLEN_STATUS];

   fits_get_errstatus(status, status_str);

   strcpy(montage_msgstr, status_str);
}



/*******************/
/*                 */
/*  Axes transform */
/*                 */
/*******************/

int mTranspose_initTransform(long *naxis, long *NAXIS)
{
   int i, j;
   int index;
   int dir;

   for(i=0; i<4; ++i)
   {
      reorder[order[i]-1] = i;

      for(j=0; j<4; ++j)
         At[i][i] = 0;

      Bt[i] = 0;

      index = fabs(order[i]-1);
      dir   = 1;

      if(order[i] < 0)
      {
         Bt[index] = naxis[i];
         dir = -1;
      }

      At[i][index] = dir;

      NAXIS[i] = naxis[index];
   }

   return 0;
}

void mTranspose_transform(int  i,  int  j,  int  k,  int  l,
                          int *it, int *jt, int *kt, int *lt)
{
   *it = At[0][0]*i + At[0][1]*j + At[0][2]*k + At[0][3]*l + Bt[0];
   *jt = At[1][0]*i + At[1][1]*j + At[1][2]*k + At[0][3]*l + Bt[1];
   *kt = At[2][0]*i + At[2][1]*j + At[2][2]*k + At[0][3]*l + Bt[2];
   *lt = At[3][0]*i + At[3][1]*j + At[3][2]*k + At[0][3]*l + Bt[3];

   return;
}
