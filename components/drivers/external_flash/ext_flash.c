/*********************
 *      INCLUDES
 *********************/

#include <sys/param.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_flash_spi_init.h"
#include "esp_partition.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"

#include "nvs_flash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ext_flash.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
 static void vspi_configuration(void);

/**********************
 *  STATIC VARIABLES
 **********************/
static wl_handle_t s_wl_handle;
static const char *TAG = "External_Flash";

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
 
/*
*This function initiliaze the VSPI bus and config the external flash memory.
*/

void ext_flash_init(void){

    /****************SPI Bus configuration*****************/

    /*We're going to use the W25Q128 external flash memory
    with only the basic pins*/
     const spi_bus_config_t bus_config = {
        .mosi_io_num = VSPI_IOMUX_PIN_NUM_MOSI,
        .miso_io_num = VSPI_IOMUX_PIN_NUM_MISO,
        .sclk_io_num = VSPI_IOMUX_PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    /* The HSPI bus is attached to the screen*/

    const esp_flash_spi_device_config_t device_config = {
        .host_id = VSPI_HOST,
        .cs_id = 0,
        .cs_io_num = VSPI_IOMUX_PIN_NUM_CS,
        .io_mode = SPI_FLASH_DIO,
        .speed = ESP_FLASH_40MHZ
    };

    ESP_LOGI(TAG, "Initializing external SPI Flash");
    ESP_LOGI(TAG, "Pin assignments:");
    ESP_LOGI(TAG, "MOSI: %2d   MISO: %2d   SCLK: %2d   CS: %2d",
        bus_config.mosi_io_num, bus_config.miso_io_num,
        bus_config.sclk_io_num, device_config.cs_io_num
    );
    
    /* Initialize the SPI bus on DMA channel 2*/
    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &bus_config, 2));

    /****************External flash init*****************/

    /* Attach the external flash to the SPI bus*/
    esp_flash_t* ext_flash;
    ESP_ERROR_CHECK(spi_bus_add_flash_device(&ext_flash, &device_config));

    /* Probe the Flash chip and initialize it*/
    esp_err_t err = esp_flash_init(ext_flash);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize external Flash: %s (0x%x)", esp_err_to_name(err), err);
        return NULL;
    }

    /*Print the ID of the chip*/
    uint32_t id;
    ESP_ERROR_CHECK(esp_flash_read_id(ext_flash, &id));
    ESP_LOGI(TAG, "Initialized external Flash, size=%d KB, ID=0x%x", ext_flash->size / 1024, id);


    /****************Add partition to the external memory*****************/

    const char partition_label = "ext_storage";

   ESP_LOGI(TAG,"Add external Flash as a partition, size=%d KB", ext_flash->size / 1024);
    const esp_partition_t* fat_partition;
    esp_partition_register_external(ext_flash, 0, ext_flash->size, "ext_storage", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, &fat_partition);

    

    /****************Mount partition with FatFS*****************/
       ESP_LOGI(TAG, "Listing data partitions:");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);

    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "- partition '%s', subtype %d, offset 0x%x, size %d kB",
        part->label, part->subtype, part->address, part->size / 1024);
    }

    esp_partition_iterator_release(it);



}

/*
*   Function to mount the Fat Filesystem, to handle in an easier way the files on the external flash
*/

void ext_flash_mount_fs(void){
    ESP_LOGI(TAG, "Mounting FAT filesystem");
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 16,
            .format_if_mount_failed = true,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    if(esp_vfs_fat_spiflash_mount("/ext_flash", "ext_storage", &mount_config, &s_wl_handle)!=ESP_OK) ESP_LOGE(TAG,"Ext_Flash mount error");
}

/*
*   Function to umount the Flash file system. Due to the Fat FS use a high amount of RAM, is recommendable to umount,
*   after use it.
*/

void ext_flash_unmount_fs(void){
    if(esp_vfs_fat_spiflash_unmount("/ext_flash",  s_wl_handle)!=ESP_OK ) ESP_LOGE(TAG,"Ext_Flash unmount error");
}

/**
* This function return the number of games available on the external flash
* and pass by reference the names of each games.
*/

uint8_t ext_flash_game_list(char * game_name){
    
    struct dirent *entry;
    
    //Open the external flash as a directory
    DIR *dir = opendir("/ext_flash/");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", "/ext_flash/");
        return 0;
    }
    // Save the name of all the files available on the external flash
    uint8_t i =0;
    while((entry = readdir(dir)) != NULL){
        sprintf(&game_name[i],"%s",entry->d_name);
        ESP_LOGI(TAG, "Found %s ",entry->d_name);

        i++;
    }
    // Return the number of files obtained
    return i;
}

/*
* This function returns the percentage of memory ussage on the external flash
*/

int ext_flash_ussage(void){
    FATFS *fs;
    size_t free_clusters;
    int res = f_getfree("0:", &free_clusters, &fs);
    assert(res == FR_OK);
    size_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    size_t free_sectors = free_clusters * fs->csize;

    // assuming the total size is < 4GiB, should be true for SPI Flash
    size_t bytes_total, bytes_free;
        bytes_total = total_sectors * fs->ssize;
    
        bytes_free = free_sectors * fs->ssize;
    
    ESP_LOGI(TAG, "FAT FS: %d kB total, %d kB free", bytes_total / 1024, bytes_free / 1024);

    //Calcule the percentage
    uint8_t percentage = 100-(100*bytes_free)/bytes_total;
    if(percentage==0) percentage=1; //The web server gives error if we percentage is 0.
    return percentage;
}

/*
* This function read a file from the external flash and copy to the partition storage of the internal flash.
* This returns a pointer to the memory direction on the internal memory wich will be handle on the GNU Boy
* loader function.
*/

char * IRAM_ATTR ext_flash_get_file (const char *path){

    char *map_ptr;// Pointer to file in the internal flash.
    spi_flash_mmap_handle_t map_handle;
    uint16_t read_chunk = 4096*2; // We split the read into 8KB
    size_t mem_offset = 0;
    char * temp_buffer; //Buffer to save on RAM the data obtained from the external flash.

    temp_buffer = malloc(read_chunk);
	esp_fill_random(temp_buffer,read_chunk); // To avoid the storage of this buffer on flash, we fill with random numbers

	// Find the partition storage on the internal flash
	const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    assert(partition != NULL);

    // Erase the partition to avoid corrupted previous data
	ESP_ERROR_CHECK(esp_partition_erase_range(partition, 0, partition->size));
	ESP_LOGI(TAG,"Partition Size %i\r\n",partition->size);
	
    //Open the file we want to copy to the internal flash
	FILE *fd = fopen(path, "rb");
	
	while(1){
		__asm__("memw");
        // Read 8K of data from the external flash and save it on the RAM
		size_t count = fread(temp_buffer, 1, read_chunk, fd);

        // Save the data storage on the RAM to the intenal FLASH
		esp_partition_write(partition, mem_offset, temp_buffer, read_chunk);
		__asm__("memw");

		mem_offset +=count;
		if(count < read_chunk) break;
	
	}
	free(temp_buffer);
	fclose(fd);
    // Return a pointer to the position of the saved file on the internal flash.
	esp_partition_mmap(partition, 0, partition->size, SPI_FLASH_MMAP_DATA, &map_ptr, &map_handle);

	return map_ptr;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
