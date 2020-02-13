/*******************************************************************************
* File Name: audio_in.c
*
*  Description: This file contains the Audio In path configuration and
*               processing code
*
******************************************************************************
* Copyright (2020), Cypress Semiconductor Corporation.
******************************************************************************
* This software is owned by Cypress Semiconductor Corporation (Cypress) and is
* protected by and subject to worldwide patent protection (United States and
* foreign), United States copyright laws and international treaty provisions.
* Cypress hereby grants to licensee a personal, non-exclusive, non-transferable
* license to copy, use, modify, create derivative works of, and compile the
* Cypress Source Code and derivative works for the sole purpose of creating
* custom software in support of licensee product to be used only in conjunction
* with a Cypress integrated circuit as specified in the applicable agreement.
* Any reproduction, modification, translation, compilation, or representation of
* this software except as specified above is prohibited without the express
* written permission of Cypress.
*
* Disclaimer: CYPRESS MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH
* REGARD TO THIS MATERIAL, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* Cypress reserves the right to make changes without further notice to the
* materials described herein. Cypress does not assume any liability arising out
* of the application or use of any product or circuit described herein. Cypress
* does not authorize its products for use as critical components in life-support
* systems where a malfunction or failure may reasonably be expected to result in
* significant injury to the user. The inclusion of Cypress' product in a life-
* support systems application implies that the manufacturer assumes all risk of
* such use and in doing so indemnifies Cypress against all charges. Use may be
* limited by and subject to the applicable Cypress software license agreement.
*****************************************************************************/

#include "audio_in.h"
#include "audio.h"
#include "usb_comm.h"

#include "cycfg.h"
#include "cy_sysint.h"

/*******************************************************************************
* Local Functions
*******************************************************************************/
void audio_in_endpoint_callback(USBFS_Type *base, 
                                uint32_t endpoint,
                                uint32_t error_type, 
                                cy_stc_usbfs_dev_drv_context_t *context);

/*******************************************************************************
* Audio In Variables
*******************************************************************************/
/* USB IN buffer data for Audio IN endpoint */
CY_USB_DEV_ALLOC_ENDPOINT_BUFFER(audio_in_usb_buffer, AUDIO_IN_ENDPOINT_SIZE);

/* PCM buffer data (16-bits) */
uint16_t audio_in_pcm_buffer[AUDIO_MAX_DATA_SIZE];

/* Current number of bytes to be transferred in the Audio IN endpoint */
volatile uint32_t audio_in_count;

/* Audio IN flags */
volatile bool audio_in_start_recording = false;
volatile bool audio_in_is_recording    = false;

/* Size of the frame */
volatile uint32_t audio_in_frame_size = AUDIO_FRAME_DATA_SIZE;

/*******************************************************************************
* Function Name: audio_in_init
********************************************************************************
* Summary:
*   Initialize the audio IN endpoint by setting up the DMAs involved and the
*   PDM/PCM block.
*
*******************************************************************************/
void audio_in_init(void)
{
    /* Register Data Endpoint Callbacks */
    Cy_USBFS_Dev_Drv_RegisterEndpointCallback(CYBSP_USBDEV_HW, 
                                              AUDIO_STREAMING_IN_ENDPOINT, 
                                              audio_in_endpoint_callback, 
                                              &usb_drvContext);

    /* Initialize the DMAs */
    Cy_DMA_Descriptor_Init(&CYBSP_DMA_PCM_Descriptor_0, &CYBSP_DMA_PCM_Descriptor_0_config);
    Cy_DMA_Channel_Init(CYBSP_DMA_PCM_HW, CYBSP_DMA_PCM_CHANNEL, &CYBSP_DMA_PCM_channelConfig);
    Cy_DMA_Enable(CYBSP_DMA_PCM_HW);
    Cy_DMA_Descriptor_SetSrcAddress(&CYBSP_DMA_PCM_Descriptor_0, (void *) &CYBSP_PDM_HW->RX_FIFO_RD);
    Cy_DMA_Descriptor_SetDstAddress(&CYBSP_DMA_PCM_Descriptor_0, (void *) audio_in_pcm_buffer);
    Cy_DMA_Channel_Enable(CYBSP_DMA_PCM_HW, CYBSP_DMA_PCM_CHANNEL);

    /* Set the count equal to the frame size */
    audio_in_count = audio_in_frame_size;

    /* Initialize the PDM PCM block */
    Cy_PDM_PCM_Init(CYBSP_PDM_HW, &CYBSP_PDM_config);
}

/*******************************************************************************
* Function Name: audio_in_enable
********************************************************************************
* Summary:
*   Starts a recording session.
*
*******************************************************************************/
void audio_in_enable(void)
{
    audio_in_start_recording  = true;
}

/*******************************************************************************
* Function Name: audio_in_disable
********************************************************************************
* Summary:
*   Stops a recording session.
*
*******************************************************************************/
void audio_in_disable(void)
{
    audio_in_is_recording = false;
}

/*******************************************************************************
* Function Name: audio_in_process
********************************************************************************
* Summary:
*   Main task for the audio in endpoint. Periodically feeds the USB Audio IN
*   endpoint.
*
*******************************************************************************/
void audio_in_process(void)
{
    if (audio_in_start_recording)
    {
        audio_in_start_recording = false;
        audio_in_is_recording = true;

        /* Clear Audio In buffer */
        memset(audio_in_pcm_buffer, 0, AUDIO_IN_ENDPOINT_SIZE);

        /* Clear PDM/PCM RX FIFO */
        Cy_PDM_PCM_ClearFifo(CYBSP_PDM_HW);

        /* Enable PDM/PCM */
        Cy_PDM_PCM_Enable(CYBSP_PDM_HW);

        /* Start a transfer to the Audio IN endpoint */
        Cy_USB_Dev_WriteEpNonBlocking(AUDIO_STREAMING_IN_ENDPOINT,
        					          (uint8_t *) audio_in_pcm_buffer,
                                      AUDIO_SAMPLE_DATA_SIZE*AUDIO_FRAME_DATA_SIZE,
                                      &usb_devContext);
    }
}

/*******************************************************************************
* Function Name: audio_in_endpoint_callback
********************************************************************************
* Summary:
*   Audio in endpoint callback implementation. It enables the Audio in DMA to
*   stream an audio frame.
*
*******************************************************************************/
void audio_in_endpoint_callback(USBFS_Type *base, 
                                uint32_t endpoint,
                                uint32_t error_type, 
                                cy_stc_usbfs_dev_drv_context_t *context)
{
	uint32_t count;

    (void) error_type;
    (void) endpoint,
    (void) context;
    (void) base;

    /* Check if should keep recording */
    if (audio_in_is_recording == true)
    {
        Cy_USB_Dev_WriteEpNonBlocking(AUDIO_STREAMING_IN_ENDPOINT,
                                      (uint8_t *) audio_in_pcm_buffer,
                                      audio_in_count*AUDIO_SAMPLE_DATA_SIZE,
                                      &usb_devContext);
    }

    /* Get the current FIFO level */
    count = Cy_PDM_PCM_GetNumInFifo(CYBSP_PDM_HW);

    /* Find out if the FIFO level is too full or too empty */
    if (count > CYBSP_PDM_config.rxFifoTriggerLevel)
    {
        /* Too many samples in the FIFO, increase the frame size */
    	audio_in_count = audio_in_frame_size + AUDIO_DELTA_VALUE;
    }
    else if (count < audio_in_frame_size)
    {
        /* Too few samples in the FIFO, decrease the frame size */
    	audio_in_count = audio_in_frame_size - AUDIO_DELTA_VALUE;
    }
    else
    {
        /* Right amount of samples in the FIFO, keep the frame size */
    	audio_in_count = audio_in_frame_size;
    }

    /* Update the DMA settings to transfer the right number of samples */
    Cy_DMA_Descriptor_SetXloopDataCount(&CYBSP_DMA_PCM_Descriptor_0, audio_in_count);
    Cy_DMA_Channel_Enable(CYBSP_DMA_PCM_HW, CYBSP_DMA_PCM_CHANNEL);
}


/* [] END OF FILE */
