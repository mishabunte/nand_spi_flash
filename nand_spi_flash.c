#include "nand_spi_flash.h"

#include "stdlib.h"
#include "string.h"

// Device Codes
#define NSF_DEVICE_TOSHIBA_TC58CVx 0x98
#define NSF_DEVICE_TC58CVG2S0HxAIx 0xCD // 4Gb 
#define NSF_DEVICE_GIGADEVICE_GD5FxGQ4x 0xC8
#define NSF_DEVICE_GD5F1GQ4R  0xA1      // 1Gb 1.8v
#define NSF_DEVICE_GD5F2GQ4R  0xA2      // 2Gb 1.8v
#define NSF_DEVICE_GD5F1GQ4U  0xB1      // 1Gb 3.3v
#define NSF_DEVICE_GD5F2GQ4U  0xB2      // 2Gb 3.3v

// Nand Flash Commands
#define NSF_CMD_MAX_BYTES 4
#define NSF_CMD_READ_ID            0x9F
#define NSF_CMD_READ_CELL_TO_CACHE 0x13
#define NSF_CMD_GET_FEATURE        0x0F
#define NSF_CMD_SET_FEATURE        0x1F
#define NSF_CMD_FEATURE_STATUS     0xC0
#define NSF_CMD_FEATURE_LOCK       0xA0
#define NSF_CMD_RESET              0xFF
#define NSF_CMD_WRITE_ENABLE       0x06
#define NSF_CMD_BLOCK_ERASE        0xD8
#define NSF_CMD_PROGRAM_LOAD       0x02
#define NSF_CMD_PROGRAM_EXECUTE    0x10

// Nand Flash Status Bits
#define NSF_OIP_MASK   0x01
#define NSF_PRG_F_MASK 0b00001000
#define NSF_ERS_F_MASK 0b00000100
#define NSF_ECC_MASK   0b00110000
#define NSF_ECC_BITS   4

// Timings
#define NSF_PAGE_READ_TIME_US 115
#define NSF_RESET_TIME_MS     7

// config instance 
static nand_spi_flash_config_t m_nsf_config;

// spi read/write buffer
static uint8_t * m_nsf_buffer = NULL;

// page size in bytes
static uint16_t m_nsf_page_size_bytes = 0;

// block size in pages
static uint16_t m_nsf_block_size_pages = 0;

// blocks count in memory
static uint16_t m_nsf_blocks_count = 0;

//-----------------------------------------------------------------------------
uint16_t nand_spi_flash_page_size_bytes() {
  return m_nsf_page_size_bytes;
}
uint16_t nand_spi_flash_block_size_pages() {
  return m_nsf_block_size_pages;
}
uint16_t nand_spi_flash_blocks_count() {
  return m_nsf_blocks_count;
}

//-----------------------------------------------------------------------------
int nand_spi_flash_init(const nand_spi_flash_config_t * config) 
{
  // check spi driver already inited and copy config
  if (m_nsf_buffer != NULL) {
    return NSF_ERR_ALREADY_INITED;
  }
  m_nsf_config = *config;
  
  // identify device
  uint8_t m_buffer[4];
  m_buffer[0] = NSF_CMD_READ_ID;
  if (m_nsf_config.spi_transfer(m_buffer, 2, 2) != 0) {
    return NSF_ERROR_SPI;
  }
  if (m_buffer[2] == NSF_DEVICE_TOSHIBA_TC58CVx) { // Toshiba
    if (m_buffer[3] == NSF_DEVICE_TC58CVG2S0HxAIx) { // TC58CVG2S0HxAIx
      m_nsf_page_size_bytes  = 4096;
      m_nsf_block_size_pages = 64;
      m_nsf_blocks_count     = 2048;
    } else {
      return NSF_ERR_UNKNOWN_DEVICE;
    }
  } else if (m_buffer[1] == NSF_DEVICE_GIGADEVICE_GD5FxGQ4x) { // gygadevice
    if (m_buffer[2] == NSF_DEVICE_GD5F1GQ4R || 
        m_buffer[2] == NSF_DEVICE_GD5F1GQ4U) 
    { // 1Gbit
      m_nsf_page_size_bytes  = 2048;
      m_nsf_block_size_pages = 64;
      m_nsf_blocks_count     = 1024;
    } else if (m_buffer[2] == NSF_DEVICE_GD5F2GQ4R || 
               m_buffer[2] == NSF_DEVICE_GD5F2GQ4U) 
    { // 2Gbit
      m_nsf_page_size_bytes  = 2048;
      m_nsf_block_size_pages = 64;
      m_nsf_blocks_count     = 2048;
    } else {
      return NSF_ERR_UNKNOWN_DEVICE;
    }
  } else {
    return NSF_ERR_UNKNOWN_DEVICE;
  }

  // allocate read/write buffer
  m_nsf_buffer = malloc(m_nsf_page_size_bytes + NSF_CMD_MAX_BYTES);

  return NSF_ERR_OK;
}

//-----------------------------------------------------------------------------
int nand_spi_flash_deinit() {
  // deallocate read/write buffer
  if (m_nsf_buffer != NULL) {
    free(m_nsf_buffer);
    m_nsf_buffer = 0;
  }
  return NSF_ERR_OK;
}

//-----------------------------------------------------------------------------
uint8_t nand_spi_flash_read_status() 
{
  m_nsf_buffer[2] = NSF_OIP_MASK;
  while(m_nsf_buffer[2] & NSF_OIP_MASK) {
    m_nsf_config.delay_us(NSF_PAGE_READ_TIME_US);
    m_nsf_buffer[0] = NSF_CMD_GET_FEATURE;
    m_nsf_buffer[1] = NSF_CMD_FEATURE_STATUS;
    m_nsf_config.spi_transfer(m_nsf_buffer, 2, 1);
  }
  return m_nsf_buffer[2];
}

//-----------------------------------------------------------------------------
int nand_spi_flash_page_read(uint32_t row_address, uint16_t col_address, 
  uint8_t * buffer, uint16_t read_len)
{
  // check data len
  if (read_len > m_nsf_page_size_bytes) {
    return NSF_ERR_DATA_TOO_BIG;
  }

  // read page to nand cache buffer 
  m_nsf_buffer[0] = NSF_CMD_READ_CELL_TO_CACHE;
  m_nsf_buffer[1] = (row_address & 0xff0000) >> 16;
  m_nsf_buffer[2] = (row_address & 0xff00) >> 8;
  m_nsf_buffer[3] = row_address; // & 0xff;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 4, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  // check status
  if ((nand_spi_flash_read_status() & NSF_ECC_MASK) == NSF_ECC_MASK) {
    return NSF_ERR_BAD_BLOCK;
  }

  // read buffer from cache
  m_nsf_buffer[0] = 0x03;
  m_nsf_buffer[1] = (col_address & 0xff00) >> 8;
  m_nsf_buffer[2] = col_address; // & 0xff;
  m_nsf_buffer[3] = 0x00;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 4, read_len) != 0) {
    return NSF_ERROR_SPI;
  }

  // copy data to output buffer
  memcpy(buffer, &m_nsf_buffer[4], read_len);

  return read_len;
}

//-----------------------------------------------------------------------------
int nand_spi_flash_reset_unlock() 
{
  // reset device
  m_nsf_buffer[0] = NSF_CMD_RESET;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 1, 0) != 0) {
    return NSF_ERROR_SPI;
  }
  nand_spi_flash_read_status();

  // unlock blocks for write
  m_nsf_buffer[0] = NSF_CMD_SET_FEATURE;
  m_nsf_buffer[1] = NSF_CMD_FEATURE_LOCK;
  m_nsf_buffer[2] = 0x00;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 3, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  return NSF_ERR_OK;
}

//-----------------------------------------------------------------------------
int nand_spi_flash_page_write(uint32_t row_address, uint16_t col_address, 
  uint8_t * data, uint16_t data_len)
{
  if (data_len + col_address > m_nsf_page_size_bytes) {
    return NSF_ERR_DATA_TOO_BIG;
  }

  // write enable
  m_nsf_buffer[0] = NSF_CMD_WRITE_ENABLE;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 1, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  // copy buffer to nand cache
  m_nsf_buffer[0] = NSF_CMD_PROGRAM_LOAD;
  m_nsf_buffer[1] = (col_address & 0xff00) >> 8;
  m_nsf_buffer[2] = col_address; // & 0xff;
  memcpy(&m_nsf_buffer[3], data, data_len);
  if (m_nsf_config.spi_transfer(m_nsf_buffer, data_len + 3, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  // program execute 0x10
  m_nsf_buffer[0] = NSF_CMD_PROGRAM_EXECUTE;
  m_nsf_buffer[1] = (row_address & 0xff0000) >> 16;
  m_nsf_buffer[2] = (row_address & 0xff00) >> 8;
  m_nsf_buffer[3] = row_address; // & 0xff;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 4, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  return (nand_spi_flash_read_status() & NSF_PRG_F_MASK) 
         ? NSF_ERR_ERASE : data_len;
}

//-----------------------------------------------------------------------------
int nand_spi_flash_block_erase(uint32_t row_address) 
{
  // enable write
  m_nsf_buffer[0] = NSF_CMD_WRITE_ENABLE;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 1, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  // erase block
  m_nsf_buffer[0] = NSF_CMD_BLOCK_ERASE;
  m_nsf_buffer[1] = (row_address & 0xff0000) >> 16;
  m_nsf_buffer[2] = (row_address & 0xff00) >> 8;
  m_nsf_buffer[3] = row_address; // & 0xff;
  if (m_nsf_config.spi_transfer(m_nsf_buffer, 4, 0) != 0) {
    return NSF_ERROR_SPI;
  }

  return (nand_spi_flash_read_status() & NSF_ERS_F_MASK) 
         ?  NSF_ERR_ERASE : NSF_ERR_OK;
}

#ifdef NAND_SPI_FLASH_STR_ERROR
//-----------------------------------------------------------------------------
const char * nand_spi_flash_str_error(int error) {
  if (error >= 0) {
    return "NSF_ERR_OK";
  }
  switch(error) {
    case -1: return "NSF_ERR_NOT_INITED";
    case -2: return "NSF_ERR_ALREADY_INITED";
    case -3: return "NSF_ERR_UNKNOWN_DEVICE";
    case -4: return "NSF_ERR_READ_ONLY";
    case -5: return "NSF_ERR_BAD_BLOCK";
    case -6: return "NSF_ERR_DATA_TOO_BIG";
    case -7: return "NSF_ERR_ERASE";
    case -8: return "NSF_ERR_PROGRAM";
    case -100: return "NSF_ERROR_SPI";
    default: return "NSF_UNKNOWN_ERROR";
  }
}
#endif

