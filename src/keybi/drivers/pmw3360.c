// Started from the JACK Enterprises Arduino example
// https://github.com/mrjohnk/PMW3360DM-T2QU/blob/master/Arduino%20Examples/PMW3360DM-polling/
//
// TODO It has quickly been converted to STM32 without extensive checks
// TODO Use the motion interrupt input
// TODO store firmware in some dedicated memory?

#include "keybi/drivers/pmw3360.h"

#include "stm32f10x_spi.h"
#include "keybi/drivers/time.h"

// Registers
#define Product_ID  0x00
#define Revision_ID 0x01
#define Motion  0x02
#define Delta_X_L 0x03
#define Delta_X_H 0x04
#define Delta_Y_L 0x05
#define Delta_Y_H 0x06
#define SQUAL 0x07
#define Raw_Data_Sum  0x08
#define Maximum_Raw_data  0x09
#define Minimum_Raw_data  0x0A
#define Shutter_Lower 0x0B
#define Shutter_Upper 0x0C
#define Control 0x0D
#define Config1 0x0F
#define Config2 0x10
#define Angle_Tune  0x11
#define Frame_Capture 0x12
#define SROM_Enable 0x13
#define Run_Downshift 0x14
#define Rest1_Rate_Lower  0x15
#define Rest1_Rate_Upper  0x16
#define Rest1_Downshift 0x17
#define Rest2_Rate_Lower  0x18
#define Rest2_Rate_Upper  0x19
#define Rest2_Downshift 0x1A
#define Rest3_Rate_Lower  0x1B
#define Rest3_Rate_Upper  0x1C
#define Observation 0x24
#define Data_Out_Lower  0x25
#define Data_Out_Upper  0x26
#define Raw_Data_Dump 0x29
#define SROM_ID 0x2A
#define Min_SQ_Run  0x2B
#define Raw_Data_Threshold  0x2C
#define Config5 0x2F
#define Power_Up_Reset  0x3A
#define Shutdown  0x3B
#define Inverse_Product_ID  0x3F
#define LiftCutoff_Tune3  0x41
#define Angle_Snap  0x42
#define LiftCutoff_Tune1  0x4A
#define Motion_Burst  0x50
#define LiftCutoff_Tune_Timeout 0x58
#define LiftCutoff_Tune_Min_Length  0x5A
#define SROM_Load_Burst 0x62
#define Lift_Config 0x63
#define Raw_Data_Burst  0x64
#define LiftCutoff_Tune2  0x65

typedef uint8_t byte;

int init_complete = 0;

static const unsigned char firmware_data[4094];

static void performStartup(void);
static byte adns_read_reg(byte reg_addr);
static void adns_write_reg(byte reg_addr, byte data);

int Keybi_Pmw3360_Init() {

    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(KEYBI_PMW3360_MT_PERIPH, ENABLE);
    GPIO_InitStructure.GPIO_Pin = KEYBI_PMW3360_MT_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEYBI_PMW3360_MT_PIN_PORT, &GPIO_InitStructure);

    RCC_APB2PeriphClockCmd(KEYBI_PMW3360_SS_PERIPH, ENABLE);
    GPIO_InitStructure.GPIO_Pin = KEYBI_PMW3360_SS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEYBI_PMW3360_SS_PIN_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    SPI_InitTypeDef SPI_InitStructure;
    SPI_StructInit (&SPI_InitStructure);

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
    //SPI_InitStructure.SPI_CRCPolynomial = 7; // TODO check was this should be
    SPI_Init(SPI2, &SPI_InitStructure);

    SPI_Cmd(SPI2 , ENABLE);

    performStartup();

    Keybi_DelayUs(1000 * 1000); // 1s (TODO was 5s but 1s is still too long I guess)

    uint8_t product_id = adns_read_reg(Product_ID);
    uint8_t inverse_product_id = adns_read_reg(Inverse_Product_ID);

    if ((product_id & 0xff) == ((~inverse_product_id) & 0xff)) {
        init_complete = 1;
    }
    return init_complete;
}

void Keybi_Pmw3360_Read(keybi_pmw3360_motion_t * motion) {
    if (init_complete) {
        //write 0x01 to Motion register and read from it to freeze
        // the motion values and make them available.
        adns_write_reg(Motion, 0x01);
        adns_read_reg(Motion);
        motion->dx = (int8_t) adns_read_reg(Delta_X_L);
        motion->dy = (int8_t) adns_read_reg(Delta_Y_L);
    } else {
        motion->dx = 0;
        motion->dy = 0;
    }
}

static uint8_t Keybi_Pmw3360_SpiTransfer(uint8_t data);

static uint8_t Keybi_Pmw3360_SpiTransfer(uint8_t data) {
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI2, data);

    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SPI2);
}

static void adns_com_begin(void);

static void adns_com_begin(){
    GPIO_ResetBits(KEYBI_PMW3360_SS_PIN_PORT,  KEYBI_PMW3360_SS_PIN);
}

static void adns_com_end(void);

static void adns_com_end(){
    GPIO_SetBits(KEYBI_PMW3360_SS_PIN_PORT,  KEYBI_PMW3360_SS_PIN);
}

static byte adns_read_reg(byte reg_addr){
  adns_com_begin();

  // send adress of the register, with MSBit = 0 to indicate it's a read
  Keybi_Pmw3360_SpiTransfer(reg_addr & 0x7f );
  Keybi_DelayUs(100); // tSRAD
  // read data
  byte data = Keybi_Pmw3360_SpiTransfer(0);

  Keybi_DelayUs(1); // tSCLK-NCS for read operation is 120ns
  adns_com_end();
  Keybi_DelayUs(19); //  tSRW/tSRR (=20us) minus tSCLK-NCS

  return data;
}

static void adns_write_reg(byte reg_addr, byte data){
  adns_com_begin();

  //send adress of the register, with MSBit = 1 to indicate it's a write
  Keybi_Pmw3360_SpiTransfer(reg_addr | 0x80 );
  //sent data
  Keybi_Pmw3360_SpiTransfer(data);

  Keybi_DelayUs(20); // tSCLK-NCS for write operation
  adns_com_end();
  Keybi_DelayUs(100); // tSWW/tSWR (=120us) minus tSCLK-NCS. Could be shortened, but is looks like a safe lower bound
}

static void adns_upload_firmware(void);

static void adns_upload_firmware(){
  // send the firmware to the chip, cf p.18 of the datasheet

  //Write 0 to Rest_En bit of Config2 register to disable Rest mode.
  adns_write_reg(Config2, 0x20);

  // write 0x1d in SROM_enable reg for initializing
  adns_write_reg(SROM_Enable, 0x1d); 

  // wait for more than one frame period
  Keybi_DelayUs(10 * 1000); // assume that the frame rate is as low as 100fps... even if it should never be that low

  // write 0x18 to SROM_enable to start SROM download
  adns_write_reg(SROM_Enable, 0x18); 

  // write the SROM file (=firmware data) 
  adns_com_begin();
  Keybi_Pmw3360_SpiTransfer(SROM_Load_Burst | 0x80); // write burst destination adress
  Keybi_DelayUs(15);

  // send all bytes of the firmware
  for(int i = 0; i < sizeof(firmware_data); i++){
    Keybi_Pmw3360_SpiTransfer(firmware_data[i]);
    Keybi_DelayUs(15);
  }

  //Read the SROM_ID register to verify the ID before any other register reads or writes.
  adns_read_reg(SROM_ID);

  //Write 0x00 to Config2 register for wired mouse or 0x20 for wireless mouse design.
  adns_write_reg(Config2, 0x00);

  // set initial CPI resolution
  adns_write_reg(Config1, 0x15);

  adns_com_end();
}


static void performStartup(void){
  adns_com_end(); // ensure that the serial port is reset
  adns_com_begin(); // ensure that the serial port is reset
  adns_com_end(); // ensure that the serial port is reset
  adns_write_reg(Power_Up_Reset, 0x5a); // force reset
  Keybi_DelayUs(50 * 1000); // wait for it to reboot
  // read registers 0x02 to 0x06 (and discard the data)
  adns_read_reg(Motion);
  adns_read_reg(Delta_X_L);
  adns_read_reg(Delta_X_H);
  adns_read_reg(Delta_Y_L);
  adns_read_reg(Delta_Y_H);
  // upload the firmware
  adns_upload_firmware();
  Keybi_DelayUs(10 * 1000);
}

static const unsigned char firmware_data[] = {
0x01, 0x04, 0x8e, 0x96, 0x6e, 0x77, 0x3e, 0xfe, 0x7e, 0x5f, 0x1d, 0xb8, 0xf2, 0x66, 0x4e,
0xff, 0x5d, 0x19, 0xb0, 0xc2, 0x04, 0x69, 0x54, 0x2a, 0xd6, 0x2e, 0xbf, 0xdd, 0x19, 0xb0,
0xc3, 0xe5, 0x29, 0xb1, 0xe0, 0x23, 0xa5, 0xa9, 0xb1, 0xc1, 0x00, 0x82, 0x67, 0x4c, 0x1a,
0x97, 0x8d, 0x79, 0x51, 0x20, 0xc7, 0x06, 0x8e, 0x7c, 0x7c, 0x7a, 0x76, 0x4f, 0xfd, 0x59,
0x30, 0xe2, 0x46, 0x0e, 0x9e, 0xbe, 0xdf, 0x1d, 0x99, 0x91, 0xa0, 0xa5, 0xa1, 0xa9, 0xd0,
0x22, 0xc6, 0xef, 0x5c, 0x1b, 0x95, 0x89, 0x90, 0xa2, 0xa7, 0xcc, 0xfb, 0x55, 0x28, 0xb3,
0xe4, 0x4a, 0xf7, 0x6c, 0x3b, 0xf4, 0x6a, 0x56, 0x2e, 0xde, 0x1f, 0x9d, 0xb8, 0xd3, 0x05,
0x88, 0x92, 0xa6, 0xce, 0x1e, 0xbe, 0xdf, 0x1d, 0x99, 0xb0, 0xe2, 0x46, 0xef, 0x5c, 0x07,
0x11, 0x5d, 0x98, 0x0b, 0x9d, 0x94, 0x97, 0xee, 0x4e, 0x45, 0x33, 0x6b, 0x44, 0xc7, 0x29,
0x56, 0x27, 0x30, 0xc6, 0xa7, 0xd5, 0xf2, 0x56, 0xdf, 0xb4, 0x38, 0x62, 0xcb, 0xa0, 0xb6,
0xe3, 0x0f, 0x84, 0x06, 0x24, 0x05, 0x65, 0x6f, 0x76, 0x89, 0xb5, 0x77, 0x41, 0x27, 0x82,
0x66, 0x65, 0x82, 0xcc, 0xd5, 0xe6, 0x20, 0xd5, 0x27, 0x17, 0xc5, 0xf8, 0x03, 0x23, 0x7c,
0x5f, 0x64, 0xa5, 0x1d, 0xc1, 0xd6, 0x36, 0xcb, 0x4c, 0xd4, 0xdb, 0x66, 0xd7, 0x8b, 0xb1,
0x99, 0x7e, 0x6f, 0x4c, 0x36, 0x40, 0x06, 0xd6, 0xeb, 0xd7, 0xa2, 0xe4, 0xf4, 0x95, 0x51,
0x5a, 0x54, 0x96, 0xd5, 0x53, 0x44, 0xd7, 0x8c, 0xe0, 0xb9, 0x40, 0x68, 0xd2, 0x18, 0xe9,
0xdd, 0x9a, 0x23, 0x92, 0x48, 0xee, 0x7f, 0x43, 0xaf, 0xea, 0x77, 0x38, 0x84, 0x8c, 0x0a,
0x72, 0xaf, 0x69, 0xf8, 0xdd, 0xf1, 0x24, 0x83, 0xa3, 0xf8, 0x4a, 0xbf, 0xf5, 0x94, 0x13,
0xdb, 0xbb, 0xd8, 0xb4, 0xb3, 0xa0, 0xfb, 0x45, 0x50, 0x60, 0x30, 0x59, 0x12, 0x31, 0x71,
0xa2, 0xd3, 0x13, 0xe7, 0xfa, 0xe7, 0xce, 0x0f, 0x63, 0x15, 0x0b, 0x6b, 0x94, 0xbb, 0x37,
0x83, 0x26, 0x05, 0x9d, 0xfb, 0x46, 0x92, 0xfc, 0x0a, 0x15, 0xd1, 0x0d, 0x73, 0x92, 0xd6,
0x8c, 0x1b, 0x8c, 0xb8, 0x55, 0x8a, 0xce, 0xbd, 0xfe, 0x8e, 0xfc, 0xed, 0x09, 0x12, 0x83,
0x91, 0x82, 0x51, 0x31, 0x23, 0xfb, 0xb4, 0x0c, 0x76, 0xad, 0x7c, 0xd9, 0xb4, 0x4b, 0xb2,
0x67, 0x14, 0x09, 0x9c, 0x7f, 0x0c, 0x18, 0xba, 0x3b, 0xd6, 0x8e, 0x14, 0x2a, 0xe4, 0x1b,
0x52, 0x9f, 0x2b, 0x7d, 0xe1, 0xfb, 0x6a, 0x33, 0x02, 0xfa, 0xac, 0x5a, 0xf2, 0x3e, 0x88,
0x7e, 0xae, 0xd1, 0xf3, 0x78, 0xe8, 0x05, 0xd1, 0xe3, 0xdc, 0x21, 0xf6, 0xe1, 0x9a, 0xbd,
0x17, 0x0e, 0xd9, 0x46, 0x9b, 0x88, 0x03, 0xea, 0xf6, 0x66, 0xbe, 0x0e, 0x1b, 0x50, 0x49,
0x96, 0x40, 0x97, 0xf1, 0xf1, 0xe4, 0x80, 0xa6, 0x6e, 0xe8, 0x77, 0x34, 0xbf, 0x29, 0x40,
0x44, 0xc2, 0xff, 0x4e, 0x98, 0xd3, 0x9c, 0xa3, 0x32, 0x2b, 0x76, 0x51, 0x04, 0x09, 0xe7,
0xa9, 0xd1, 0xa6, 0x32, 0xb1, 0x23, 0x53, 0xe2, 0x47, 0xab, 0xd6, 0xf5, 0x69, 0x5c, 0x3e,
0x5f, 0xfa, 0xae, 0x45, 0x20, 0xe5, 0xd2, 0x44, 0xff, 0x39, 0x32, 0x6d, 0xfd, 0x27, 0x57,
0x5c, 0xfd, 0xf0, 0xde, 0xc1, 0xb5, 0x99, 0xe5, 0xf5, 0x1c, 0x77, 0x01, 0x75, 0xc5, 0x6d,
0x58, 0x92, 0xf2, 0xb2, 0x47, 0x00, 0x01, 0x26, 0x96, 0x7a, 0x30, 0xff, 0xb7, 0xf0, 0xef,
0x77, 0xc1, 0x8a, 0x5d, 0xdc, 0xc0, 0xd1, 0x29, 0x30, 0x1e, 0x77, 0x38, 0x7a, 0x94, 0xf1,
0xb8, 0x7a, 0x7e, 0xef, 0xa4, 0xd1, 0xac, 0x31, 0x4a, 0xf2, 0x5d, 0x64, 0x3d, 0xb2, 0xe2,
0xf0, 0x08, 0x99, 0xfc, 0x70, 0xee, 0x24, 0xa7, 0x7e, 0xee, 0x1e, 0x20, 0x69, 0x7d, 0x44,
0xbf, 0x87, 0x42, 0xdf, 0x88, 0x3b, 0x0c, 0xda, 0x42, 0xc9, 0x04, 0xf9, 0x45, 0x50, 0xfc,
0x83, 0x8f, 0x11, 0x6a, 0x72, 0xbc, 0x99, 0x95, 0xf0, 0xac, 0x3d, 0xa7, 0x3b, 0xcd, 0x1c,
0xe2, 0x88, 0x79, 0x37, 0x11, 0x5f, 0x39, 0x89, 0x95, 0x0a, 0x16, 0x84, 0x7a, 0xf6, 0x8a,
0xa4, 0x28, 0xe4, 0xed, 0x83, 0x80, 0x3b, 0xb1, 0x23, 0xa5, 0x03, 0x10, 0xf4, 0x66, 0xea,
0xbb, 0x0c, 0x0f, 0xc5, 0xec, 0x6c, 0x69, 0xc5, 0xd3, 0x24, 0xab, 0xd4, 0x2a, 0xb7, 0x99,
0x88, 0x76, 0x08, 0xa0, 0xa8, 0x95, 0x7c, 0xd8, 0x38, 0x6d, 0xcd, 0x59, 0x02, 0x51, 0x4b,
0xf1, 0xb5, 0x2b, 0x50, 0xe3, 0xb6, 0xbd, 0xd0, 0x72, 0xcf, 0x9e, 0xfd, 0x6e, 0xbb, 0x44,
0xc8, 0x24, 0x8a, 0x77, 0x18, 0x8a, 0x13, 0x06, 0xef, 0x97, 0x7d, 0xfa, 0x81, 0xf0, 0x31,
0xe6, 0xfa, 0x77, 0xed, 0x31, 0x06, 0x31, 0x5b, 0x54, 0x8a, 0x9f, 0x30, 0x68, 0xdb, 0xe2,
0x40, 0xf8, 0x4e, 0x73, 0xfa, 0xab, 0x74, 0x8b, 0x10, 0x58, 0x13, 0xdc, 0xd2, 0xe6, 0x78,
0xd1, 0x32, 0x2e, 0x8a, 0x9f, 0x2c, 0x58, 0x06, 0x48, 0x27, 0xc5, 0xa9, 0x5e, 0x81, 0x47,
0x89, 0x46, 0x21, 0x91, 0x03, 0x70, 0xa4, 0x3e, 0x88, 0x9c, 0xda, 0x33, 0x0a, 0xce, 0xbc,
0x8b, 0x8e, 0xcf, 0x9f, 0xd3, 0x71, 0x80, 0x43, 0xcf, 0x6b, 0xa9, 0x51, 0x83, 0x76, 0x30,
0x82, 0xc5, 0x6a, 0x85, 0x39, 0x11, 0x50, 0x1a, 0x82, 0xdc, 0x1e, 0x1c, 0xd5, 0x7d, 0xa9,
0x71, 0x99, 0x33, 0x47, 0x19, 0x97, 0xb3, 0x5a, 0xb1, 0xdf, 0xed, 0xa4, 0xf2, 0xe6, 0x26,
0x84, 0xa2, 0x28, 0x9a, 0x9e, 0xdf, 0xa6, 0x6a, 0xf4, 0xd6, 0xfc, 0x2e, 0x5b, 0x9d, 0x1a,
0x2a, 0x27, 0x68, 0xfb, 0xc1, 0x83, 0x21, 0x4b, 0x90, 0xe0, 0x36, 0xdd, 0x5b, 0x31, 0x42,
0x55, 0xa0, 0x13, 0xf7, 0xd0, 0x89, 0x53, 0x71, 0x99, 0x57, 0x09, 0x29, 0xc5, 0xf3, 0x21,
0xf8, 0x37, 0x2f, 0x40, 0xf3, 0xd4, 0xaf, 0x16, 0x08, 0x36, 0x02, 0xfc, 0x77, 0xc5, 0x8b,
0x04, 0x90, 0x56, 0xb9, 0xc9, 0x67, 0x9a, 0x99, 0xe8, 0x00, 0xd3, 0x86, 0xff, 0x97, 0x2d,
0x08, 0xe9, 0xb7, 0xb3, 0x91, 0xbc, 0xdf, 0x45, 0xc6, 0xed, 0x0f, 0x8c, 0x4c, 0x1e, 0xe6,
0x5b, 0x6e, 0x38, 0x30, 0xe4, 0xaa, 0xe3, 0x95, 0xde, 0xb9, 0xe4, 0x9a, 0xf5, 0xb2, 0x55,
0x9a, 0x87, 0x9b, 0xf6, 0x6a, 0xb2, 0xf2, 0x77, 0x9a, 0x31, 0xf4, 0x7a, 0x31, 0xd1, 0x1d,
0x04, 0xc0, 0x7c, 0x32, 0xa2, 0x9e, 0x9a, 0xf5, 0x62, 0xf8, 0x27, 0x8d, 0xbf, 0x51, 0xff,
0xd3, 0xdf, 0x64, 0x37, 0x3f, 0x2a, 0x6f, 0x76, 0x3a, 0x7d, 0x77, 0x06, 0x9e, 0x77, 0x7f,
0x5e, 0xeb, 0x32, 0x51, 0xf9, 0x16, 0x66, 0x9a, 0x09, 0xf3, 0xb0, 0x08, 0xa4, 0x70, 0x96,
0x46, 0x30, 0xff, 0xda, 0x4f, 0xe9, 0x1b, 0xed, 0x8d, 0xf8, 0x74, 0x1f, 0x31, 0x92, 0xb3,
0x73, 0x17, 0x36, 0xdb, 0x91, 0x30, 0xd6, 0x88, 0x55, 0x6b, 0x34, 0x77, 0x87, 0x7a, 0xe7,
0xee, 0x06, 0xc6, 0x1c, 0x8c, 0x19, 0x0c, 0x48, 0x46, 0x23, 0x5e, 0x9c, 0x07, 0x5c, 0xbf,
0xb4, 0x7e, 0xd6, 0x4f, 0x74, 0x9c, 0xe2, 0xc5, 0x50, 0x8b, 0xc5, 0x8b, 0x15, 0x90, 0x60,
0x62, 0x57, 0x29, 0xd0, 0x13, 0x43, 0xa1, 0x80, 0x88, 0x91, 0x00, 0x44, 0xc7, 0x4d, 0x19,
0x86, 0xcc, 0x2f, 0x2a, 0x75, 0x5a, 0xfc, 0xeb, 0x97, 0x2a, 0x70, 0xe3, 0x78, 0xd8, 0x91,
0xb0, 0x4f, 0x99, 0x07, 0xa3, 0x95, 0xea, 0x24, 0x21, 0xd5, 0xde, 0x51, 0x20, 0x93, 0x27,
0x0a, 0x30, 0x73, 0xa8, 0xff, 0x8a, 0x97, 0xe9, 0xa7, 0x6a, 0x8e, 0x0d, 0xe8, 0xf0, 0xdf,
0xec, 0xea, 0xb4, 0x6c, 0x1d, 0x39, 0x2a, 0x62, 0x2d, 0x3d, 0x5a, 0x8b, 0x65, 0xf8, 0x90,
0x05, 0x2e, 0x7e, 0x91, 0x2c, 0x78, 0xef, 0x8e, 0x7a, 0xc1, 0x2f, 0xac, 0x78, 0xee, 0xaf,
0x28, 0x45, 0x06, 0x4c, 0x26, 0xaf, 0x3b, 0xa2, 0xdb, 0xa3, 0x93, 0x06, 0xb5, 0x3c, 0xa5,
0xd8, 0xee, 0x8f, 0xaf, 0x25, 0xcc, 0x3f, 0x85, 0x68, 0x48, 0xa9, 0x62, 0xcc, 0x97, 0x8f,
0x7f, 0x2a, 0xea, 0xe0, 0x15, 0x0a, 0xad, 0x62, 0x07, 0xbd, 0x45, 0xf8, 0x41, 0xd8, 0x36,
0xcb, 0x4c, 0xdb, 0x6e, 0xe6, 0x3a, 0xe7, 0xda, 0x15, 0xe9, 0x29, 0x1e, 0x12, 0x10, 0xa0,
0x14, 0x2c, 0x0e, 0x3d, 0xf4, 0xbf, 0x39, 0x41, 0x92, 0x75, 0x0b, 0x25, 0x7b, 0xa3, 0xce,
0x39, 0x9c, 0x15, 0x64, 0xc8, 0xfa, 0x3d, 0xef, 0x73, 0x27, 0xfe, 0x26, 0x2e, 0xce, 0xda,
0x6e, 0xfd, 0x71, 0x8e, 0xdd, 0xfe, 0x76, 0xee, 0xdc, 0x12, 0x5c, 0x02, 0xc5, 0x3a, 0x4e,
0x4e, 0x4f, 0xbf, 0xca, 0x40, 0x15, 0xc7, 0x6e, 0x8d, 0x41, 0xf1, 0x10, 0xe0, 0x4f, 0x7e,
0x97, 0x7f, 0x1c, 0xae, 0x47, 0x8e, 0x6b, 0xb1, 0x25, 0x31, 0xb0, 0x73, 0xc7, 0x1b, 0x97,
0x79, 0xf9, 0x80, 0xd3, 0x66, 0x22, 0x30, 0x07, 0x74, 0x1e, 0xe4, 0xd0, 0x80, 0x21, 0xd6,
0xee, 0x6b, 0x6c, 0x4f, 0xbf, 0xf5, 0xb7, 0xd9, 0x09, 0x87, 0x2f, 0xa9, 0x14, 0xbe, 0x27,
0xd9, 0x72, 0x50, 0x01, 0xd4, 0x13, 0x73, 0xa6, 0xa7, 0x51, 0x02, 0x75, 0x25, 0xe1, 0xb3,
0x45, 0x34, 0x7d, 0xa8, 0x8e, 0xeb, 0xf3, 0x16, 0x49, 0xcb, 0x4f, 0x8c, 0xa1, 0xb9, 0x36,
0x85, 0x39, 0x75, 0x5d, 0x08, 0x00, 0xae, 0xeb, 0xf6, 0xea, 0xd7, 0x13, 0x3a, 0x21, 0x5a,
0x5f, 0x30, 0x84, 0x52, 0x26, 0x95, 0xc9, 0x14, 0xf2, 0x57, 0x55, 0x6b, 0xb1, 0x10, 0xc2,
0xe1, 0xbd, 0x3b, 0x51, 0xc0, 0xb7, 0x55, 0x4c, 0x71, 0x12, 0x26, 0xc7, 0x0d, 0xf9, 0x51,
0xa4, 0x38, 0x02, 0x05, 0x7f, 0xb8, 0xf1, 0x72, 0x4b, 0xbf, 0x71, 0x89, 0x14, 0xf3, 0x77,
0x38, 0xd9, 0x71, 0x24, 0xf3, 0x00, 0x11, 0xa1, 0xd8, 0xd4, 0x69, 0x27, 0x08, 0x37, 0x35,
0xc9, 0x11, 0x9d, 0x90, 0x1c, 0x0e, 0xe7, 0x1c, 0xff, 0x2d, 0x1e, 0xe8, 0x92, 0xe1, 0x18,
0x10, 0x95, 0x7c, 0xe0, 0x80, 0xf4, 0x96, 0x43, 0x21, 0xf9, 0x75, 0x21, 0x64, 0x38, 0xdd,
0x9f, 0x1e, 0x95, 0x16, 0xda, 0x56, 0x1d, 0x4f, 0x9a, 0x53, 0xb2, 0xe2, 0xe4, 0x18, 0xcb,
0x6b, 0x1a, 0x65, 0xeb, 0x56, 0xc6, 0x3b, 0xe5, 0xfe, 0xd8, 0x26, 0x3f, 0x3a, 0x84, 0x59,
0x72, 0x66, 0xa2, 0xf3, 0x75, 0xff, 0xfb, 0x60, 0xb3, 0x22, 0xad, 0x3f, 0x2d, 0x6b, 0xf9,
0xeb, 0xea, 0x05, 0x7c, 0xd8, 0x8f, 0x6d, 0x2c, 0x98, 0x9e, 0x2b, 0x93, 0xf1, 0x5e, 0x46,
0xf0, 0x87, 0x49, 0x29, 0x73, 0x68, 0xd7, 0x7f, 0xf9, 0xf0, 0xe5, 0x7d, 0xdb, 0x1d, 0x75,
0x19, 0xf3, 0xc4, 0x58, 0x9b, 0x17, 0x88, 0xa8, 0x92, 0xe0, 0xbe, 0xbd, 0x8b, 0x1d, 0x8d,
0x9f, 0x56, 0x76, 0xad, 0xaf, 0x29, 0xe2, 0xd9, 0xd5, 0x52, 0xf6, 0xb5, 0x56, 0x35, 0x57,
0x3a, 0xc8, 0xe1, 0x56, 0x43, 0x19, 0x94, 0xd3, 0x04, 0x9b, 0x6d, 0x35, 0xd8, 0x0b, 0x5f,
0x4d, 0x19, 0x8e, 0xec, 0xfa, 0x64, 0x91, 0x0a, 0x72, 0x20, 0x2b, 0xbc, 0x1a, 0x4a, 0xfe,
0x8b, 0xfd, 0xbb, 0xed, 0x1b, 0x23, 0xea, 0xad, 0x72, 0x82, 0xa1, 0x29, 0x99, 0x71, 0xbd,
0xf0, 0x95, 0xc1, 0x03, 0xdd, 0x7b, 0xc2, 0xb2, 0x3c, 0x28, 0x54, 0xd3, 0x68, 0xa4, 0x72,
0xc8, 0x66, 0x96, 0xe0, 0xd1, 0xd8, 0x7f, 0xf8, 0xd1, 0x26, 0x2b, 0xf7, 0xad, 0xba, 0x55,
0xca, 0x15, 0xb9, 0x32, 0xc3, 0xe5, 0x88, 0x97, 0x8e, 0x5c, 0xfb, 0x92, 0x25, 0x8b, 0xbf,
0xa2, 0x45, 0x55, 0x7a, 0xa7, 0x6f, 0x8b, 0x57, 0x5b, 0xcf, 0x0e, 0xcb, 0x1d, 0xfb, 0x20,
0x82, 0x77, 0xa8, 0x8c, 0xcc, 0x16, 0xce, 0x1d, 0xfa, 0xde, 0xcc, 0x0b, 0x62, 0xfe, 0xcc,
0xe1, 0xb7, 0xf0, 0xc3, 0x81, 0x64, 0x73, 0x40, 0xa0, 0xc2, 0x4d, 0x89, 0x11, 0x75, 0x33,
0x55, 0x33, 0x8d, 0xe8, 0x4a, 0xfd, 0xea, 0x6e, 0x30, 0x0b, 0xd7, 0x31, 0x2c, 0xde, 0x47,
0xe3, 0xbf, 0xf8, 0x55, 0x42, 0xe2, 0x7f, 0x59, 0xe5, 0x17, 0xef, 0x99, 0x34, 0x69, 0x91,
0xb1, 0x23, 0x8e, 0x20, 0x87, 0x2d, 0xa8, 0xfe, 0xd5, 0x8a, 0xf3, 0x84, 0x3a, 0xf0, 0x37,
0xe4, 0x09, 0x00, 0x54, 0xee, 0x67, 0x49, 0x93, 0xe4, 0x81, 0x70, 0xe3, 0x90, 0x4d, 0xef,
0xfe, 0x41, 0xb7, 0x99, 0x7b, 0xc1, 0x83, 0xba, 0x62, 0x12, 0x6f, 0x7d, 0xde, 0x6b, 0xaf,
0xda, 0x16, 0xf9, 0x55, 0x51, 0xee, 0xa6, 0x0c, 0x2b, 0x02, 0xa3, 0xfd, 0x8d, 0xfb, 0x30,
0x17, 0xe4, 0x6f, 0xdf, 0x36, 0x71, 0xc4, 0xca, 0x87, 0x25, 0x48, 0xb0, 0x47, 0xec, 0xea,
0xb4, 0xbf, 0xa5, 0x4d, 0x9b, 0x9f, 0x02, 0x93, 0xc4, 0xe3, 0xe4, 0xe8, 0x42, 0x2d, 0x68,
0x81, 0x15, 0x0a, 0xeb, 0x84, 0x5b, 0xd6, 0xa8, 0x74, 0xfb, 0x7d, 0x1d, 0xcb, 0x2c, 0xda,
0x46, 0x2a, 0x76, 0x62, 0xce, 0xbc, 0x5c, 0x9e, 0x8b, 0xe7, 0xcf, 0xbe, 0x78, 0xf5, 0x7c,
0xeb, 0xb3, 0x3a, 0x9c, 0xaa, 0x6f, 0xcc, 0x72, 0xd1, 0x59, 0xf2, 0x11, 0x23, 0xd6, 0x3f,
0x48, 0xd1, 0xb7, 0xce, 0xb0, 0xbf, 0xcb, 0xea, 0x80, 0xde, 0x57, 0xd4, 0x5e, 0x97, 0x2f,
0x75, 0xd1, 0x50, 0x8e, 0x80, 0x2c, 0x66, 0x79, 0xbf, 0x72, 0x4b, 0xbd, 0x8a, 0x81, 0x6c,
0xd3, 0xe1, 0x01, 0xdc, 0xd2, 0x15, 0x26, 0xc5, 0x36, 0xda, 0x2c, 0x1a, 0xc0, 0x27, 0x94,
0xed, 0xb7, 0x9b, 0x85, 0x0b, 0x5e, 0x80, 0x97, 0xc5, 0xec, 0x4f, 0xec, 0x88, 0x5d, 0x50,
0x07, 0x35, 0x47, 0xdc, 0x0b, 0x3b, 0x3d, 0xdd, 0x60, 0xaf, 0xa8, 0x5d, 0x81, 0x38, 0x24,
0x25, 0x5d, 0x5c, 0x15, 0xd1, 0xde, 0xb3, 0xab, 0xec, 0x05, 0x69, 0xef, 0x83, 0xed, 0x57,
0x54, 0xb8, 0x64, 0x64, 0x11, 0x16, 0x32, 0x69, 0xda, 0x9f, 0x2d, 0x7f, 0x36, 0xbb, 0x44,
0x5a, 0x34, 0xe8, 0x7f, 0xbf, 0x03, 0xeb, 0x00, 0x7f, 0x59, 0x68, 0x22, 0x79, 0xcf, 0x73,
0x6c, 0x2c, 0x29, 0xa7, 0xa1, 0x5f, 0x38, 0xa1, 0x1d, 0xf0, 0x20, 0x53, 0xe0, 0x1a, 0x63,
0x14, 0x58, 0x71, 0x10, 0xaa, 0x08, 0x0c, 0x3e, 0x16, 0x1a, 0x60, 0x22, 0x82, 0x7f, 0xba,
0xa4, 0x43, 0xa0, 0xd0, 0xac, 0x1b, 0xd5, 0x6b, 0x64, 0xb5, 0x14, 0x93, 0x31, 0x9e, 0x53,
0x50, 0xd0, 0x57, 0x66, 0xee, 0x5a, 0x4f, 0xfb, 0x03, 0x2a, 0x69, 0x58, 0x76, 0xf1, 0x83,
0xf7, 0x4e, 0xba, 0x8c, 0x42, 0x06, 0x60, 0x5d, 0x6d, 0xce, 0x60, 0x88, 0xae, 0xa4, 0xc3,
0xf1, 0x03, 0xa5, 0x4b, 0x98, 0xa1, 0xff, 0x67, 0xe1, 0xac, 0xa2, 0xb8, 0x62, 0xd7, 0x6f,
0xa0, 0x31, 0xb4, 0xd2, 0x77, 0xaf, 0x21, 0x10, 0x06, 0xc6, 0x9a, 0xff, 0x1d, 0x09, 0x17,
0x0e, 0x5f, 0xf1, 0xaa, 0x54, 0x34, 0x4b, 0x45, 0x8a, 0x87, 0x63, 0xa6, 0xdc, 0xf9, 0x24,
0x30, 0x67, 0xc6, 0xb2, 0xd6, 0x61, 0x33, 0x69, 0xee, 0x50, 0x61, 0x57, 0x28, 0xe7, 0x7e,
0xee, 0xec, 0x3a, 0x5a, 0x73, 0x4e, 0xa8, 0x8d, 0xe4, 0x18, 0xea, 0xec, 0x41, 0x64, 0xc8,
0xe2, 0xe8, 0x66, 0xb6, 0x2d, 0xb6, 0xfb, 0x6a, 0x6c, 0x16, 0xb3, 0xdd, 0x46, 0x43, 0xb9,
0x73, 0x00, 0x6a, 0x71, 0xed, 0x4e, 0x9d, 0x25, 0x1a, 0xc3, 0x3c, 0x4a, 0x95, 0x15, 0x99,
0x35, 0x81, 0x14, 0x02, 0xd6, 0x98, 0x9b, 0xec, 0xd8, 0x23, 0x3b, 0x84, 0x29, 0xaf, 0x0c,
0x99, 0x83, 0xa6, 0x9a, 0x34, 0x4f, 0xfa, 0xe8, 0xd0, 0x3c, 0x4b, 0xd0, 0xfb, 0xb6, 0x68,
0xb8, 0x9e, 0x8f, 0xcd, 0xf7, 0x60, 0x2d, 0x7a, 0x22, 0xe5, 0x7d, 0xab, 0x65, 0x1b, 0x95,
0xa7, 0xa8, 0x7f, 0xb6, 0x77, 0x47, 0x7b, 0x5f, 0x8b, 0x12, 0x72, 0xd0, 0xd4, 0x91, 0xef,
0xde, 0x19, 0x50, 0x3c, 0xa7, 0x8b, 0xc4, 0xa9, 0xb3, 0x23, 0xcb, 0x76, 0xe6, 0x81, 0xf0,
0xc1, 0x04, 0x8f, 0xa3, 0xb8, 0x54, 0x5b, 0x97, 0xac, 0x19, 0xff, 0x3f, 0x55, 0x27, 0x2f,
0xe0, 0x1d, 0x42, 0x9b, 0x57, 0xfc, 0x4b, 0x4e, 0x0f, 0xce, 0x98, 0xa9, 0x43, 0x57, 0x03,
0xbd, 0xe7, 0xc8, 0x94, 0xdf, 0x6e, 0x36, 0x73, 0x32, 0xb4, 0xef, 0x2e, 0x85, 0x7a, 0x6e,
0xfc, 0x6c, 0x18, 0x82, 0x75, 0x35, 0x90, 0x07, 0xf3, 0xe4, 0x9f, 0x3e, 0xdc, 0x68, 0xf3,
0xb5, 0xf3, 0x19, 0x80, 0x92, 0x06, 0x99, 0xa2, 0xe8, 0x6f, 0xff, 0x2e, 0x7f, 0xae, 0x42,
0xa4, 0x5f, 0xfb, 0xd4, 0x0e, 0x81, 0x2b, 0xc3, 0x04, 0xff, 0x2b, 0xb3, 0x74, 0x4e, 0x36,
0x5b, 0x9c, 0x15, 0x00, 0xc6, 0x47, 0x2b, 0xe8, 0x8b, 0x3d, 0xf1, 0x9c, 0x03, 0x9a, 0x58,
0x7f, 0x9b, 0x9c, 0xbf, 0x85, 0x49, 0x79, 0x35, 0x2e, 0x56, 0x7b, 0x41, 0x14, 0x39, 0x47,
0x83, 0x26, 0xaa, 0x07, 0x89, 0x98, 0x11, 0x1b, 0x86, 0xe7, 0x73, 0x7a, 0xd8, 0x7d, 0x78,
0x61, 0x53, 0xe9, 0x79, 0xf5, 0x36, 0x8d, 0x44, 0x92, 0x84, 0xf9, 0x13, 0x50, 0x58, 0x3b,
0xa4, 0x6a, 0x36, 0x65, 0x49, 0x8e, 0x3c, 0x0e, 0xf1, 0x6f, 0xd2, 0x84, 0xc4, 0x7e, 0x8e,
0x3f, 0x39, 0xae, 0x7c, 0x84, 0xf1, 0x63, 0x37, 0x8e, 0x3c, 0xcc, 0x3e, 0x44, 0x81, 0x45,
0xf1, 0x4b, 0xb9, 0xed, 0x6b, 0x36, 0x5d, 0xbb, 0x20, 0x60, 0x1a, 0x0f, 0xa3, 0xaa, 0x55,
0x77, 0x3a, 0xa9, 0xae, 0x37, 0x4d, 0xba, 0xb8, 0x86, 0x6b, 0xbc, 0x08, 0x50, 0xf6, 0xcc,
0xa4, 0xbd, 0x1d, 0x40, 0x72, 0xa5, 0x86, 0xfa, 0xe2, 0x10, 0xae, 0x3d, 0x58, 0x4b, 0x97,
0xf3, 0x43, 0x74, 0xa9, 0x9e, 0xeb, 0x21, 0xb7, 0x01, 0xa4, 0x86, 0x93, 0x97, 0xee, 0x2f,
0x4f, 0x3b, 0x86, 0xa1, 0x41, 0x6f, 0x41, 0x26, 0x90, 0x78, 0x5c, 0x7f, 0x30, 0x38, 0x4b,
0x3f, 0xaa, 0xec, 0xed, 0x5c, 0x6f, 0x0e, 0xad, 0x43, 0x87, 0xfd, 0x93, 0x35, 0xe6, 0x01,
0xef, 0x41, 0x26, 0x90, 0x99, 0x9e, 0xfb, 0x19, 0x5b, 0xad, 0xd2, 0x91, 0x8a, 0xe0, 0x46,
0xaf, 0x65, 0xfa, 0x4f, 0x84, 0xc1, 0xa1, 0x2d, 0xcf, 0x45, 0x8b, 0xd3, 0x85, 0x50, 0x55,
0x7c, 0xf9, 0x67, 0x88, 0xd4, 0x4e, 0xe9, 0xd7, 0x6b, 0x61, 0x54, 0xa1, 0xa4, 0xa6, 0xa2,
0xc2, 0xbf, 0x30, 0x9c, 0x40, 0x9f, 0x5f, 0xd7, 0x69, 0x2b, 0x24, 0x82, 0x5e, 0xd9, 0xd6,
0xa7, 0x12, 0x54, 0x1a, 0xf7, 0x55, 0x9f, 0x76, 0x50, 0xa9, 0x95, 0x84, 0xe6, 0x6b, 0x6d,
0xb5, 0x96, 0x54, 0xd6, 0xcd, 0xb3, 0xa1, 0x9b, 0x46, 0xa7, 0x94, 0x4d, 0xc4, 0x94, 0xb4,
0x98, 0xe3, 0xe1, 0xe2, 0x34, 0xd5, 0x33, 0x16, 0x07, 0x54, 0xcd, 0xb7, 0x77, 0x53, 0xdb,
0x4f, 0x4d, 0x46, 0x9d, 0xe9, 0xd4, 0x9c, 0x8a, 0x36, 0xb6, 0xb8, 0x38, 0x26, 0x6c, 0x0e,
0xff, 0x9c, 0x1b, 0x43, 0x8b, 0x80, 0xcc, 0xb9, 0x3d, 0xda, 0xc7, 0xf1, 0x8a, 0xf2, 0x6d,
0xb8, 0xd7, 0x74, 0x2f, 0x7e, 0x1e, 0xb7, 0xd3, 0x4a, 0xb4, 0xac, 0xfc, 0x79, 0x48, 0x6c,
0xbc, 0x96, 0xb6, 0x94, 0x46, 0x57, 0x2d, 0xb0, 0xa3, 0xfc, 0x1e, 0xb9, 0x52, 0x60, 0x85,
0x2d, 0x41, 0xd0, 0x43, 0x01, 0x1e, 0x1c, 0xd5, 0x7d, 0xfc, 0xf3, 0x96, 0x0d, 0xc7, 0xcb,
0x2a, 0x29, 0x9a, 0x93, 0xdd, 0x88, 0x2d, 0x37, 0x5d, 0xaa, 0xfb, 0x49, 0x68, 0xa0, 0x9c,
0x50, 0x86, 0x7f, 0x68, 0x56, 0x57, 0xf9, 0x79, 0x18, 0x39, 0xd4, 0xe0, 0x01, 0x84, 0x33,
0x61, 0xca, 0xa5, 0xd2, 0xd6, 0xe4, 0xc9, 0x8a, 0x4a, 0x23, 0x44, 0x4e, 0xbc, 0xf0, 0xdc,
0x24, 0xa1, 0xa0, 0xc4, 0xe2, 0x07, 0x3c, 0x10, 0xc4, 0xb5, 0x25, 0x4b, 0x65, 0x63, 0xf4,
0x80, 0xe7, 0xcf, 0x61, 0xb1, 0x71, 0x82, 0x21, 0x87, 0x2c, 0xf5, 0x91, 0x00, 0x32, 0x0c,
0xec, 0xa9, 0xb5, 0x9a, 0x74, 0x85, 0xe3, 0x36, 0x8f, 0x76, 0x4f, 0x9c, 0x6d, 0xce, 0xbc,
0xad, 0x0a, 0x4b, 0xed, 0x76, 0x04, 0xcb, 0xc3, 0xb9, 0x33, 0x9e, 0x01, 0x93, 0x96, 0x69,
0x7d, 0xc5, 0xa2, 0x45, 0x79, 0x9b, 0x04, 0x5c, 0x84, 0x09, 0xed, 0x88, 0x43, 0xc7, 0xab,
0x93, 0x14, 0x26, 0xa1, 0x40, 0xb5, 0xce, 0x4e, 0xbf, 0x2a, 0x42, 0x85, 0x3e, 0x2c, 0x3b,
0x54, 0xe8, 0x12, 0x1f, 0x0e, 0x97, 0x59, 0xb2, 0x27, 0x89, 0xfa, 0xf2, 0xdf, 0x8e, 0x68,
0x59, 0xdc, 0x06, 0xbc, 0xb6, 0x85, 0x0d, 0x06, 0x22, 0xec, 0xb1, 0xcb, 0xe5, 0x04, 0xe6,
0x3d, 0xb3, 0xb0, 0x41, 0x73, 0x08, 0x3f, 0x3c, 0x58, 0x86, 0x63, 0xeb, 0x50, 0xee, 0x1d,
0x2c, 0x37, 0x74, 0xa9, 0xd3, 0x18, 0xa3, 0x47, 0x6e, 0x93, 0x54, 0xad, 0x0a, 0x5d, 0xb8,
0x2a, 0x55, 0x5d, 0x78, 0xf6, 0xee, 0xbe, 0x8e, 0x3c, 0x76, 0x69, 0xb9, 0x40, 0xc2, 0x34,
0xec, 0x2a, 0xb9, 0xed, 0x7e, 0x20, 0xe4, 0x8d, 0x00, 0x38, 0xc7, 0xe6, 0x8f, 0x44, 0xa8,
0x86, 0xce, 0xeb, 0x2a, 0xe9, 0x90, 0xf1, 0x4c, 0xdf, 0x32, 0xfb, 0x73, 0x1b, 0x6d, 0x92,
0x1e, 0x95, 0xfe, 0xb4, 0xdb, 0x65, 0xdf, 0x4d, 0x23, 0x54, 0x89, 0x48, 0xbf, 0x4a, 0x2e,
0x70, 0xd6, 0xd7, 0x62, 0xb4, 0x33, 0x29, 0xb1, 0x3a, 0x33, 0x4c, 0x23, 0x6d, 0xa6, 0x76,
0xa5, 0x21, 0x63, 0x48, 0xe6, 0x90, 0x5d, 0xed, 0x90, 0x95, 0x0b, 0x7a, 0x84, 0xbe, 0xb8,
0x0d, 0x5e, 0x63, 0x0c, 0x62, 0x26, 0x4c, 0x14, 0x5a, 0xb3, 0xac, 0x23, 0xa4, 0x74, 0xa7,
0x6f, 0x33, 0x30, 0x05, 0x60, 0x01, 0x42, 0xa0, 0x28, 0xb7, 0xee, 0x19, 0x38, 0xf1, 0x64,
0x80, 0x82, 0x43, 0xe1, 0x41, 0x27, 0x1f, 0x1f, 0x90, 0x54, 0x7a, 0xd5, 0x23, 0x2e, 0xd1,
0x3d, 0xcb, 0x28, 0xba, 0x58, 0x7f, 0xdc, 0x7c, 0x91, 0x24, 0xe9, 0x28, 0x51, 0x83, 0x6e,
0xc5, 0x56, 0x21, 0x42, 0xed, 0xa0, 0x56, 0x22, 0xa1, 0x40, 0x80, 0x6b, 0xa8, 0xf7, 0x94,
0xca, 0x13, 0x6b, 0x0c, 0x39, 0xd9, 0xfd, 0xe9, 0xf3, 0x6f, 0xa6, 0x9e, 0xfc, 0x70, 0x8a,
0xb3, 0xbc, 0x59, 0x3c, 0x1e, 0x1d, 0x6c, 0xf9, 0x7c, 0xaf, 0xf9, 0x88, 0x71, 0x95, 0xeb,
0x57, 0x00, 0xbd, 0x9f, 0x8c, 0x4f, 0xe1, 0x24, 0x83, 0xc5, 0x22, 0xea, 0xfd, 0xd3, 0x0c,
0xe2, 0x17, 0x18, 0x7c, 0x6a, 0x4c, 0xde, 0x77, 0xb4, 0x53, 0x9b, 0x4c, 0x81, 0xcd, 0x23,
0x60, 0xaa, 0x0e, 0x25, 0x73, 0x9c, 0x02, 0x79, 0x32, 0x30, 0xdf, 0x74, 0xdf, 0x75, 0x19,
0xf4, 0xa5, 0x14, 0x5c, 0xf7, 0x7a, 0xa8, 0xa5, 0x91, 0x84, 0x7c, 0x60, 0x03, 0x06, 0x3b,
0xcd, 0x50, 0xb6, 0x27, 0x9c, 0xfe, 0xb1, 0xdd, 0xcc, 0xd3, 0xb0, 0x59, 0x24, 0xb2, 0xca,
0xe2, 0x1c, 0x81, 0x22, 0x9d, 0x07, 0x8f, 0x8e, 0xb9, 0xbe, 0x4e, 0xfa, 0xfc, 0x39, 0x65,
0xba, 0xbf, 0x9d, 0x12, 0x37, 0x5e, 0x97, 0x7e, 0xf3, 0x89, 0xf5, 0x5d, 0xf5, 0xe3, 0x09,
0x8c, 0x62, 0xb5, 0x20, 0x9d, 0x0c, 0x53, 0x8a, 0x68, 0x1b, 0xd2, 0x8f, 0x75, 0x17, 0x5d,
0xd4, 0xe5, 0xda, 0x75, 0x62, 0x19, 0x14, 0x6a, 0x26, 0x2d, 0xeb, 0xf8, 0xaf, 0x37, 0xf0,
0x6c, 0xa4, 0x55, 0xb1, 0xbc, 0xe2, 0x33, 0xc0, 0x9a, 0xca, 0xb0, 0x11, 0x49, 0x4f, 0x68,
0x9b, 0x3b, 0x6b, 0x3c, 0xcc, 0x13, 0xf6, 0xc7, 0x85, 0x61, 0x68, 0x42, 0xae, 0xbb, 0xdd,
0xcd, 0x45, 0x16, 0x29, 0x1d, 0xea, 0xdb, 0xc8, 0x03, 0x94, 0x3c, 0xee, 0x4f, 0x82, 0x11,
0xc3, 0xec, 0x28, 0xbd, 0x97, 0x05, 0x99, 0xde, 0xd7, 0xbb, 0x5e, 0x22, 0x1f, 0xd4, 0xeb,
0x64, 0xd9, 0x92, 0xd9, 0x85, 0xb7, 0x6a, 0x05, 0x6a, 0xe4, 0x24, 0x41, 0xf1, 0xcd, 0xf0,
0xd8, 0x3f, 0xf8, 0x9e, 0x0e, 0xcd, 0x0b, 0x7a, 0x70, 0x6b, 0x5a, 0x75, 0x0a, 0x6a, 0x33,
0x88, 0xec, 0x17, 0x75, 0x08, 0x70, 0x10, 0x2f, 0x24, 0xcf, 0xc4, 0xe9, 0x42, 0x00, 0x61,
0x94, 0xca, 0x1f, 0x3a, 0x76, 0x06, 0xfa, 0xd2, 0x48, 0x81, 0xf0, 0x77, 0x60, 0x03, 0x45,
0xd9, 0x61, 0xf4, 0xa4, 0x6f, 0x3d, 0xd9, 0x30, 0xc3, 0x04, 0x6b, 0x54, 0x2a, 0xb7, 0xec,
0x3b, 0xf4, 0x4b, 0xf5, 0x68, 0x52, 0x26, 0xce, 0xff, 0x5d, 0x19, 0x91, 0xa0, 0xa3, 0xa5,
0xa9, 0xb1, 0xe0, 0x23, 0xc4, 0x0a, 0x77, 0x4d, 0xf9, 0x51, 0x20, 0xa3, 0xa5, 0xa9, 0xb1,
0xc1, 0x00, 0x82, 0x86, 0x8e, 0x7f, 0x5d, 0x19, 0x91, 0xa0, 0xa3, 0xc4, 0xeb, 0x54, 0x0b,
0x75, 0x68, 0x52, 0x07, 0x8c, 0x9a, 0x97, 0x8d, 0x79, 0x70, 0x62, 0x46, 0xef, 0x5c, 0x1b,
0x95, 0x89, 0x71, 0x41, 0xe1, 0x21, 0xa1, 0xa1, 0xa1, 0xc0, 0x02, 0x67, 0x4c, 0x1a, 0xb6,
0xcf, 0xfd, 0x78, 0x53, 0x24, 0xab, 0xb5, 0xc9, 0xf1, 0x60, 0x23, 0xa5, 0xc8, 0x12, 0x87,
0x6d, 0x58, 0x13, 0x85, 0x88, 0x92, 0x87, 0x6d, 0x58, 0x32, 0xc7, 0x0c, 0x9a, 0x97, 0xac,
0xda, 0x36, 0xee, 0x5e, 0x3e, 0xdf, 0x1d, 0xb8, 0xf2, 0x66, 0x2f, 0xbd, 0xf8, 0x72, 0x47,
0xed, 0x58, 0x13, 0x85, 0x88, 0x92, 0x87, 0x8c, 0x7b, 0x55, 0x09, 0x90, 0xa2, 0xc6, 0xef,
0x3d, 0xf8, 0x53, 0x24, 0xab, 0xd4, 0x2a, 0xb7, 0xec, 0x5a, 0x36, 0xee, 0x5e, 0x3e, 0xdf,
0x3c, 0xfa, 0x76, 0x4f, 0xfd, 0x59, 0x30, 0xe2, 0x46, 0xef, 0x3d, 0xf8, 0x53, 0x05, 0x69,
0x31, 0xc1, 0x00, 0x82, 0x86, 0x8e, 0x7f, 0x5d, 0x19, 0xb0, 0xe2, 0x27, 0xcc, 0xfb, 0x74,
0x4b, 0x14, 0x8b, 0x94, 0x8b, 0x75, 0x68, 0x33, 0xc5, 0x08, 0x92, 0x87, 0x8c, 0x9a, 0xb6,
0xcf, 0x1c, 0xba, 0xd7, 0x0d, 0x98, 0xb2, 0xe6, 0x2f, 0xdc, 0x1b, 0x95, 0x89, 0x71, 0x60,
0x23, 0xc4, 0x0a, 0x96, 0x8f, 0x9c, 0xba, 0xf6, 0x6e, 0x3f, 0xfc, 0x5b, 0x15, 0xa8, 0xd2,
0x26, 0xaf, 0xbd, 0xf8, 0x72, 0x66, 0x2f, 0xdc, 0x1b, 0xb4, 0xcb, 0x14, 0x8b, 0x94, 0xaa,
0xb7, 0xcd, 0xf9, 0x51, 0x01, 0x80, 0x82, 0x86, 0x6f, 0x3d, 0xd9, 0x30, 0xe2, 0x27, 0xcc,
0xfb, 0x74, 0x4b, 0x14, 0xaa, 0xb7, 0xcd, 0xf9, 0x70, 0x43, 0x04, 0x6b, 0x35, 0xc9, 0xf1,
0x60, 0x23, 0xa5, 0xc8, 0xf3, 0x45, 0x08, 0x92, 0x87, 0x6d, 0x58, 0x32, 0xe6, 0x2f, 0xbd,
0xf8, 0x72, 0x66, 0x4e, 0x1e, 0xbe, 0xfe, 0x7e, 0x7e, 0x7e, 0x5f, 0x1d, 0x99, 0x91, 0xa0,
0xa3, 0xc4, 0x0a, 0x77, 0x4d, 0x18, 0x93, 0xa4, 0xab, 0xd4, 0x0b, 0x75, 0x49, 0x10, 0xa2,
0xc6, 0xef, 0x3d, 0xf8, 0x53, 0x24, 0xab, 0xb5, 0xe8, 0x33, 0xe4, 0x4a, 0x16, 0xae, 0xde,
0x1f, 0xbc, 0xdb, 0x15, 0xa8, 0xb3, 0xc5, 0x08, 0x73, 0x45, 0xe9, 0x31, 0xc1, 0xe1, 0x21,
0xa1, 0xa1, 0xa1, 0xc0, 0x02, 0x86, 0x6f, 0x5c, 0x3a, 0xd7, 0x0d, 0x98, 0x93, 0xa4, 0xca,
0x16, 0xae, 0xde, 0x1f, 0x9d, 0x99, 0xb0, 0xe2, 0x46, 0xef, 0x3d, 0xf8, 0x72, 0x47, 0x0c,
0x9a, 0xb6, 0xcf, 0xfd, 0x59, 0x11, 0xa0, 0xa3, 0xa5, 0xc8, 0xf3, 0x45, 0x08, 0x92, 0x87,
0x6d, 0x39, 0xf0, 0x43, 0x04, 0x8a, 0x96, 0xae, 0xde, 0x3e, 0xdf, 0x1d, 0x99, 0x91, 0xa0,
0xc2, 0x06, 0x6f, 0x3d, 0xf8, 0x72, 0x47, 0x0c, 0x9a, 0x97, 0x8d, 0x98, 0x93, 0x85, 0x88,
0x73, 0x45, 0xe9, 0x31, 0xe0, 0x23, 0xa5, 0xa9, 0xd0, 0x03, 0x84, 0x8a, 0x96, 0xae, 0xde,
0x1f, 0xbc, 0xdb, 0x15, 0xa8, 0xd2, 0x26, 0xce, 0xff, 0x5d, 0x19, 0x91, 0x81, 0x80, 0x82,
0x67, 0x2d, 0xd8, 0x13, 0xa4, 0xab, 0xd4, 0x0b, 0x94, 0xaa, 0xb7, 0xcd, 0xf9, 0x51, 0x20,
0xa3, 0xa5, 0xc8, 0xf3, 0x45, 0xe9, 0x50, 0x22, 0xc6, 0xef, 0x5c, 0x3a, 0xd7, 0x0d, 0x98,
0x93, 0x85, 0x88, 0x73, 0x64, 0x4a, 0xf7, 0x4d, 0xf9, 0x51, 0x20, 0xa3, 0xc4, 0x0a, 0x96,
0xae, 0xde, 0x3e, 0xfe, 0x7e, 0x7e, 0x7e, 0x5f, 0x3c, 0xfa, 0x76, 0x4f, 0xfd, 0x78, 0x72,
0x66, 0x2f, 0xbd, 0xd9, 0x30, 0xc3, 0xe5, 0x48, 0x12, 0x87, 0x8c, 0x7b, 0x55, 0x28, 0xd2,
0x07, 0x8c, 0x9a, 0x97, 0xac, 0xda, 0x17, 0x8d, 0x79, 0x51, 0x20, 0xa3, 0xc4, 0xeb, 0x54,
0x0b, 0x94, 0x8b, 0x94, 0xaa, 0xd6, 0x2e, 0xbf, 0xfc, 0x5b, 0x15, 0xa8, 0xd2, 0x26, 0xaf,
0xdc, 0x1b, 0xb4, 0xea, 0x37, 0xec, 0x3b, 0xf4, 0x6a, 0x37, 0xcd, 0x18, 0x93, 0x85, 0x69,
0x31, 0xc1, 0xe1, 0x40, 0xe3, 0x25, 0xc8, 0x12, 0x87, 0x8c, 0x9a, 0xb6, 0xcf, 0xfd, 0x59,
0x11, 0xa0, 0xc2, 0x06, 0x8e, 0x7f, 0x5d, 0x38, 0xf2, 0x47, 0x0c, 0x7b, 0x74, 0x6a, 0x37,
0xec, 0x5a, 0x36, 0xee, 0x3f, 0xfc, 0x7a, 0x76, 0x4f, 0x1c, 0x9b, 0x95, 0x89, 0x71, 0x41,
0x00, 0x63, 0x44, 0xeb, 0x54, 0x2a, 0xd6, 0x0f, 0x9c, 0xba, 0xd7, 0x0d, 0x98, 0x93, 0x85,
0x69, 0x31, 0xc1, 0x00, 0x82, 0x86, 0x8e, 0x9e, 0xbe, 0xdf, 0x3c, 0xfa, 0x57, 0x2c, 0xda,
0x36, 0xee, 0x3f, 0xfc, 0x5b, 0x15, 0x89, 0x71, 0x41, 0x00, 0x82, 0x86, 0x8e, 0x7f, 0x5d,
0x38, 0xf2, 0x47, 0xed, 0x58, 0x13, 0xa4, 0xca, 0xf7, 0x4d, 0xf9, 0x51, 0x01, 0x80, 0x63,
0x44, 0xeb, 0x54, 0x2a, 0xd6, 0x2e, 0xbf, 0xdd, 0x19, 0x91, 0xa0, 0xa3, 0xa5, 0xa9, 0xb1,
0xe0, 0x42, 0x06, 0x8e, 0x7f, 0x5d, 0x19, 0x91, 0xa0, 0xa3, 0xc4, 0x0a, 0x96, 0x8f, 0x7d,
0x78, 0x72, 0x47, 0x0c, 0x7b, 0x74, 0x6a, 0x56, 0x2e, 0xde, 0x1f, 0xbc, 0xfa, 0x57, 0x0d,
0x79, 0x51, 0x01, 0x61, 0x21, 0xa1, 0xc0, 0xe3, 0x25, 0xa9, 0xb1, 0xc1, 0xe1, 0x40, 0x02,
0x67, 0x4c, 0x1a, 0x97, 0x8d, 0x98, 0x93, 0xa4, 0xab, 0xd4, 0x2a, 0xd6, 0x0f, 0x9c, 0x9b,
0xb4, 0xcb, 0x14, 0xaa, 0xb7, 0xcd, 0xf9, 0x51, 0x20, 0xa3, 0xc4, 0xeb, 0x35, 0xc9, 0xf1,
0x60, 0x42, 0x06, 0x8e, 0x7f, 0x7c, 0x7a, 0x76, 0x6e, 0x3f, 0xfc, 0x7a, 0x76, 0x6e, 0x5e,
0x3e, 0xfe, 0x7e, 0x5f, 0x3c, 0xdb, 0x15, 0x89, 0x71, 0x41, 0xe1, 0x21, 0xc0, 0xe3, 0x44,
0xeb, 0x54, 0x2a, 0xb7, 0xcd, 0xf9, 0x70, 0x62, 0x27, 0xad, 0xd8, 0x32, 0xc7, 0x0c, 0x7b,
0x74, 0x4b, 0x14, 0xaa, 0xb7, 0xec, 0x3b, 0xd5, 0x28, 0xd2, 0x07, 0x6d, 0x39, 0xd1, 0x20,
0xc2, 0xe7, 0x4c, 0x1a, 0x97, 0x8d, 0x98, 0xb2, 0xc7, 0x0c, 0x59, 0x28, 0xf3, 0x9b };
