#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include <mShrinkCube.h>
#include <montage.h>

#define MAXSTR 256

extern char *optarg;
extern int optind, opterr;

extern int getopt(int argc, char *const *argv, const char *options);


int main(int argc, char **argv)
{
   int       c, debug, hdu, fixedSize, mfactor;
   double    shrinkFactor;

   char      input_file [MAXSTR];
   char      output_file[MAXSTR];

   char     *end;

   struct mShrinkCubeReturn *returnStruct;

   FILE *montage_status;


   /***************************************/
   /* Process the command-line parameters */
   /***************************************/

   debug     = 0;
   fixedSize = 0;
   hdu       = 0;
   mfactor   = 1;
   
   opterr = 0;

   montage_status = stdout;

   while ((c = getopt(argc, argv, "d:h:m:s:f")) != EOF) 
   {
      switch (c) 
      {
         case 'd':
            debug = montage_debugCheck(optarg);

            if(debug < 0)
            {
                fprintf(montage_status, "[struct stat=\"ERROR\", msg=\"Invalid debug level.\"]\n");
                exit(1);
            }
            break;

         case 'h':
            hdu = strtol(optarg, &end, 10);

            if(end < optarg + strlen(optarg) || hdu < 0)
            {
               printf("[struct stat=\"ERROR\", msg=\"HDU value (%s) must be a non-negative integer\"]\n",
                  optarg);
               exit(1);
            }
            break;
            
         case 'm':
            mfactor = strtol(optarg, &end, 10);

            if(end < optarg + strlen(optarg) || mfactor <= 0)
            {
               printf("[struct stat=\"ERROR\", msg=\"Third axis compression factor (%s) must be a positive integer\"]\n",
                  optarg);
               exit(1);
            }
            break;
            
         case 's':
           if((montage_status = fopen(optarg, "w+")) == (FILE *)NULL)
            {
               printf("[struct stat=\"ERROR\", msg=\"Cannot open status file: %s\"]\n",
                  optarg);
               exit(1);
            }
            break;

         case 'f':
            fixedSize = 1;
            break;

         default:
            printf("[struct stat=\"ERROR\", msg=\"Usage: %s [-f(ixed-size)] [-d level] [-h hdu] [-m factor] [-s statusfile] in.fits out.fits factor\"]\n", argv[0]);
            exit(1);
            break;
      }
   }

   if (argc - optind < 3) 
   {
      fprintf(montage_status, "[struct stat=\"ERROR\", msg=\"Usage: %s [-f(ixed-size)] [-d level] [-h hdu] [-m factor] [-s statusfile] in.fits out.fits factor\"]\n", argv[0]);
      exit(1);
   }
  
   strcpy(input_file,    argv[optind]);
   strcpy(output_file,   argv[optind + 1]);

   shrinkFactor = strtod(argv[optind + 2], &end);

   if(end < argv[optind + 2] + strlen(argv[optind + 2]))
   {
      printf("[struct stat=\"ERROR\", msg=\"Shrink factor (%s) cannot be interpreted as an real number\"]\n",
         argv[optind + 2]);
      exit(1);
   }

   returnStruct = mShrinkCube(input_file, hdu, output_file, shrinkFactor, mfactor, fixedSize, debug);

   if(returnStruct->status == 1)
   {
       fprintf(montage_status, "[struct stat=\"ERROR\", msg=\"%s\"]\n", returnStruct->msg);
       exit(1);
   }
   else
   {
       fprintf(montage_status, "[struct stat=\"OK\", %s]\n", returnStruct->msg);
       exit(0);
   }
}
