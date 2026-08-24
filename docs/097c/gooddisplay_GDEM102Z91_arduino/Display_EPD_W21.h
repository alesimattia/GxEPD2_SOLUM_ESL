#ifndef _DISPLAY_EPD_W21_H_
#define _DISPLAY_EPD_W21_H_


#define EPD_WIDTH   960
#define EPD_HEIGHT  640

#define EPD_ARRAY  EPD_WIDTH*EPD_HEIGHT/8 
//EPD
void EPD_HW_Init(void); 
void EPD_HW_Init_180(void);  
void EPD_WhiteScreen_ALL(const unsigned char* datasBW,const unsigned char* datasRW);
void EPD_WhiteScreen_White(void);
void EPD_WhiteScreen_Black(void);
void EPD_DeepSleep(void);
void EPD_Update(void);
#endif
/***********************************************************
            end file
***********************************************************/
