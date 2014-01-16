#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XO2_dev.h"


#define TRUE 1
#define FALSE 0

#define MAX_LINE 1023
#define STX 0x02
#define ETX 0x03

enum {
   COMMENT,
   FUSE_CHECKSUM,
   FUSE_DATA,
   END_DATA,
   FUSE_LIST,
   FUSE_STATE,
   SECURITY_FUSE,
   FUSE_DEFAULT,
   FUSE_SIZE,
   USER_CODE,
   FEATURE_ROW,
   DONE
};


void    convertFeatureRowToHexArray(char *p, void *buf, int cnt);
void    convertFuseToHexArray(char *p, void *buf) ;
void    findDeviceType(char *pS, XO2Devices_t *pDevID);

int  convertBinaryField(char *p, unsigned int *pVal) {
   unsigned int val;
   unsigned int i;

   val = 0;
   while ((*p != '\0') && (i < 32)) {
      val = (val << 1) | (*p - '0');
      ++i;
   }
   *pVal = val;
   return (i / 8);
}



void removeLastStar(char *p) {
   int i;
   i = 0;
   while (*p != '\0') {
      ++i;
      ++p;
   }

   if (i == 0) return;  // empty string

   while ((*p != '*') && i) {
      --p;
      --i;
   }
   if (i) *p = '\0';  // replace last '*' with NULL, end line at that spot
}




int main(int argc, char *argv[]) {
   FILE * fpIn,*fpOut;
   unsigned char flag;
   unsigned char frame[17];
   unsigned char line[MAX_LINE + 1];
   unsigned char *cfgdata = NULL;
   unsigned char *umfdata = NULL;
   int i, j;
   int pageCnt = 0, UFMPageCnt= 0, CfgPageCnt = 0;
   int CfgPageTotal = 0,UFMPageTotal = 0; 
   int rle;
   int compress = TRUE;
   int done;
   int state;
   int fuse_addr;
   int fuse_size;
   int erase_val;
   int security_fuse;
   unsigned int userCode;
   char *pName;
   char outFileStr[256];
   XO2Devices_t devID;
   XO2FeatureRow_t featurerow;
   

   devID =	0;
   if (argv[1] == NULL) {
      printf("Please assign JED file path as argv[1]");
      exit(-5);
   }
   if (argv[2] == NULL) {
      printf("Please assign output file name as argv[2]");
      exit(-5);
   }
   fpIn = fopen(argv[1], "r");
   if (fpIn == NULL) {
      perror("Opening input file");
      exit(-1);
   }

   pName = argv[2];
   sprintf(outFileStr, "%s", pName); 
   printf("Convert %s to %s\r\n" ,argv[1],outFileStr);
   fpOut = fopen(outFileStr, "wb+");
   if (fpOut == NULL) {
      fclose(fpIn);
      perror("Opening output file");
      exit(-1);
   }
   fgets(line, MAX_LINE, fpIn);
   if (line[0] != STX) {
      printf("ERROR!  Expected STX as first char!\nAborting.\n");
      fclose(fpIn);
      fclose(fpOut);
      exit(-2);
   }


   done = FALSE;
   while (!feof(fpIn) && !done) {
      if (fgets(line, MAX_LINE, fpIn) != NULL) {

         if ((line[0] == '0') || (line[0] == '1')) state = FUSE_DATA;
         else if (strncmp("NOTE", line, 4) == 0) state = COMMENT;
         else if (line[0] == 'G') state = SECURITY_FUSE;
         else if (line[0] == 'L') state = FUSE_LIST;
         else if (line[0] == 'F') state = FUSE_STATE;
         else if (line[0] == 'C') state = FUSE_CHECKSUM; 
         else if (line[0] == '*') state = END_DATA;
         else if (line[0] == 'D') state = FUSE_DEFAULT;
         else if (line[0] == 'U') state = USER_CODE;
         else if (line[0] == 'E') state = FEATURE_ROW;
         else if (strncmp("QF", line, 2) == 0) state = FUSE_SIZE;
         else if (line[0] == ETX) state = DONE;

         switch (state) {
         case FUSE_DATA:
            if ((cfgdata == NULL) || (umfdata == NULL)) {
               printf("ERROR! Not find DEVICE NAME in input JED file.\n");
               fclose(fpIn);
               fclose(fpOut);
               system("PAUSE");
               exit(-2);
            }
            ++pageCnt;
            if (pageCnt <= CfgPageTotal) {
               convertFuseToHexArray(line, &cfgdata[16*CfgPageCnt++]);
            } else if(UFMPageCnt < UFMPageTotal) {
               convertFuseToHexArray(line, &umfdata[16*UFMPageCnt++]);
            }
            break;

         case COMMENT:
            //ignore,skip to next line
            break;

         case FUSE_LIST:
            removeLastStar(line);
            sscanf(&line[1], "%d", &fuse_addr);
            break;

         case SECURITY_FUSE:
            removeLastStar(line);
            sscanf(&line[1], "%d", &security_fuse);
            printf("Security Fuse: %x\n", security_fuse);
            break;

         case FUSE_STATE:
            break;

         case FUSE_DEFAULT:
            removeLastStar(line);
            sscanf(&line[1], "%d", &erase_val);
            if (erase_val != 0) printf("WARNING!  DEFAULT ERASE STATE NOT 0!\n");
            break;

         case FUSE_SIZE:
            removeLastStar(line);
            sscanf(&line[2], "%d", &fuse_size);
            break;

         case USER_CODE:   // This is informational only.  USERCODE is part of Config sector
            removeLastStar(line);
            if (line[1] == 'H') sscanf(&line[2], "%x", &userCode);
            else convertBinaryField(&line[1], &userCode);
            break;

         case FEATURE_ROW:
            // 2 consectutive rows.  1st starts with E and is 64 bits.  2nd line is 16 bits, ends in *
            convertFeatureRowToHexArray(&line[1], featurerow.feature, sizeof(featurerow.feature));
            fgets(line, MAX_LINE, fpIn);
            removeLastStar(line);
            convertFeatureRowToHexArray(&line[0], featurerow.feabits, sizeof(featurerow.feabits));
            break;

         case DONE:
            done = TRUE;
            break;

         case END_DATA:
            break;

         case FUSE_CHECKSUM:
            break;

         default:
            // do nothing
            break;

         }
         // Look for specific XO2 Device type and extract
         if ((state == COMMENT) && (strncmp("DEVICE NAME:", &line[5], 12) == 0)) {
            removeLastStar(line);
            findDeviceType(&line[17], &devID);
            if ((unsigned int)devID >= sizeof(XO2DevList)/sizeof(XO2DevInfo_t)) {
               printf("Noknow Device\r\n");
               fclose(fpIn);
               fclose(fpOut);
               exit(-5);
            }
            UFMPageTotal = XO2DevList[devID].UFMpages; 
            CfgPageTotal = XO2DevList[devID].Cfgpages;  
            printf("DEVICE ID :\t%d\r\n", devID); 
            printf("CONFIG PAGE:\t%d\r\n",CfgPageTotal);
            printf("UFM PAGE:\t%d \r\n", UFMPageTotal);
            cfgdata = (unsigned char *)malloc(CfgPageTotal * 16);
            umfdata = (unsigned char *)malloc(UFMPageTotal * 16);
         }
      }
   }

   XO2_JEDEC_t jedec;
   printf("DEVICE ID :\t%d\r\n", devID);
   jedec.devID = devID;
   jedec.pageCnt = pageCnt;
   jedec.CfgDataSize = CfgPageTotal * 16;
   jedec.UFMDataSize = UFMPageTotal * 16;
   jedec.UserCode = userCode;
   jedec.SecurityFuses = security_fuse;
   jedec.pCfgData = (void *)sizeof(jedec);
   jedec.pUFMData = (void *)(sizeof(jedec) + jedec.CfgDataSize);
   jedec.pFeatureRow = (void *)(sizeof(jedec) + jedec.CfgDataSize+jedec.UFMDataSize);
   fwrite(&jedec, sizeof(jedec), 1, fpOut);
   //write cfgdate to output file
   fwrite(cfgdata, jedec.CfgDataSize, 1, fpOut);
   //write umfdate to output file
   fwrite(umfdata, jedec.UFMDataSize, 1, fpOut);
   //write featurerow to output file
   fwrite(&featurerow, sizeof(XO2FeatureRow_t), 1, fpOut);

   fclose(fpIn);
   fclose(fpOut);
   printf("Convert success!!!");
   if (cfgdata !=NULL) {
      free(cfgdata);
   }
   if (umfdata !=NULL) {
      free(umfdata);
   }
   return 0; 
}





/**
 * Convert a line of fuse data into a list of C byte values in an array.
 * This function is specific to the MachXO2 format of 128 bits per page (fuse row).
 * So it just processes knowing that there are 16 bytes per row.
 *
 * JEDEC file format is lsb first so shift into byte from top down.
 */
void    convertFuseToHexArray(char *p, void *buf) {
   unsigned char val;
   int i, j;
   unsigned char *buftemp = buf;

   for (i = 0; i < 16; i++) {
      val = 0;
      for (j = 0; j < 8; j++) {
         val = (val << 1) | (*p - '0');
         ++p;
      }
      buftemp[i] = val;
   }
}


/**
 * Convert the E Field (Feature Row) into a list of C byte values in an array.
 * This function is specific to the MachXO2 Feature Row format.
 * The number of bytes in a row is passed in.
 * the bits are actually in reverse order so we need to work backwards through
 * the string.
 *
 * JEDEC file format is lsb first so shift into byte from top down.
 */
void    convertFeatureRowToHexArray(char *p, void *buf, int cnt) {
   unsigned char val;
   int i, j;
   unsigned char *buftemp = buf;
   // start at last char in string and work backwards
   p = p + ((8 * cnt) - 1);

   for (i = 0; i < cnt; i++) {
      val = 0;
      for (j = 0; j < 8; j++) {
         val = (val << 1) | (*p - '0');

         --p;
      }
      buftemp[i] = val;
   }
}


// TODO
// Still need to handle the U devices and their increased sizes.

//	MachXO2_256,
//	MachXO2_640,
//	MachXO2_640U,
//	MachXO2_1200,
//	MachXO2_1200U,
//	MachXO2_2000,
//	MachXO2_2000U,
//	Mach_XO2_4000,
//	MachXO2_7000
void    findDeviceType(char *pS, XO2Devices_t *pDevID) {
   printf("XO2 Dev: %s\n", pS);
   if (strstr(pS, "LCMXO2-256") != NULL) *pDevID = MachXO2_256;
   else if (strstr(pS, "LCMXO2-640") != NULL) *pDevID = MachXO2_640;
   else if (strstr(pS, "LCMXO2-1200") != NULL) *pDevID = MachXO2_1200;
   else if (strstr(pS, "LCMXO2-2000") != NULL) *pDevID = MachXO2_2000;
   else if (strstr(pS, "LCMXO2-4000") != NULL) *pDevID = MachXO2_4000;
   else if (strstr(pS, "LCMXO2-7000") != NULL) *pDevID = MachXO2_7000;
   else *pDevID = MachXO2_1200;   // our default
}
