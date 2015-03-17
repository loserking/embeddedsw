/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * All rights reserved.
 *
 * Copyright (C) 2015 Xilinx, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of Mentor Graphics Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************************
* This is a sample demonstration application that showcases usage of rpmsg
* This application is meant to run on the remote CPU running bare-metal code.
* It echoes back data that was sent to it by the master core.
*
* The application calls init_system which defines a shared memory region in
* MPU settings for the communication between master and remote using
* zynqMP_r5_map_mem_region API,it also initializes interrupt controller
* GIC and register the interrupt service routine for IPI using
* zynqMP_r5_gic_initialize API.
*
* Echo test calls the remoteproc_resource_init API to create the
* virtio/RPMsg devices required for IPC with the master context.
* Invocation of this API causes remoteproc on the bare-metal to use the
* rpmsg name service announcement feature to advertise the rpmsg channels
* served by the application.
*
* The master receives the advertisement messages and performs the following tasks:
* 	1. Invokes the channel created callback registered by the master application
* 	2. Responds to remote context with a name service acknowledgement message
* After the acknowledgement is received from master, remoteproc on the bare-metal
* invokes the RPMsg channel-created callback registered by the remote application.
* The RPMsg channel is established at this point. All RPMsg APIs can be used subsequently
* on both sides for run time communications between the master and remote software contexts.
*
* Upon running the master application to send data to remote core, master will
* generate the payload and send to remote (bare-metal) by informing the bare-metal with
* an IPI, the remote will send the data back by master and master will perform a check
* whether the same data is received. Once the application is ran and task by the
* bare-metal application is done, master needs to properly shut down the remote
* processor
*
* To shut down the remote processor, the following steps are performed:
* 	1. The master application sends an application-specific shut-down message
* 	   to the remote context
* 	2. This bare-metal application cleans up application resources,
* 	   sends a shut-down acknowledge to master, and invokes remoteproc_resource_deinit
* 	   API to de-initialize remoteproc on the bare-metal side.
* 	3. On receiving the shut-down acknowledge message, the master application invokes
* 	   the remoteproc_shutdown API to shut down the remote processor and de-initialize
* 	   remoteproc using remoteproc_deinit on its side.
*
**************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "open_amp.h"
#include "rsc_table.h"
#include "baremetal.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include "xstatus.h"
#include "xreg_cortexr5.h"

#define SHUTDOWN_MSG	0xEF56A55A

/*
 * Shared memory location as defined in linux device tree for remoteproc
 * User may need to check with device tree of remoteproc and ensure the
 * share memory address is same
 */
#define SHARED_MEMORY 	0x3ED00000
#define SHARED_SIZE 	0x400000	/* size of the shared memory*/

/* Internal functions */
static void rpmsg_channel_created(struct rpmsg_channel *rp_chnl);
static void rpmsg_channel_deleted(struct rpmsg_channel *rp_chnl);
static void rpmsg_read_cb(struct rpmsg_channel *, void *, int, void *, unsigned long);
static void init_system();

/* Globals */
static struct rpmsg_channel *app_rp_chnl;
static struct rpmsg_endpoint *rp_ept;
static struct remote_proc *proc = NULL;
static struct rsc_table_info rsc_info;
extern const struct remote_resource_table resources;

/* Application entry point */
int main() {

    /* Initialize HW system components */
    init_system();

	/*
	 * The data caches are disabled due to some unusual behavior
	 * Upon running the application second time without rebooting,
	 * communication channel is not being established. It is a known
	 * issue and need to be fixed in future.
	 */
	Xil_DCacheDisable();

    rsc_info.rsc_tab = (struct resource_table *)&resources;
    rsc_info.size = sizeof(resources);

    /* Initialize RPMSG framework */
    remoteproc_resource_init(&rsc_info, rpmsg_channel_created, rpmsg_channel_deleted, rpmsg_read_cb,
                    &proc);

    while(1) {
		 __asm__ ( "\
			wfi\n\t" \
		);
	};

    return 0;
}

static void rpmsg_channel_created(struct rpmsg_channel *rp_chnl) {
    app_rp_chnl = rp_chnl;
    rp_ept = rpmsg_create_ept(rp_chnl, rpmsg_read_cb, RPMSG_NULL,
                    RPMSG_ADDR_ANY);
}

static void rpmsg_channel_deleted(struct rpmsg_channel *rp_chnl) {

}

static void rpmsg_read_cb(struct rpmsg_channel *rp_chnl, void *data, int len,
                void * priv, unsigned long src) {
    if ((*(int *) data) == SHUTDOWN_MSG) {
        remoteproc_resource_deinit(proc);
    } else {
        /* Send data back to master*/
        rpmsg_send(rp_chnl, data, len);
    }
}

static void init_system() {

	/* configure MPU for shared memory region */
	zynqMP_r5_map_mem_region(SHARED_MEMORY, SHARED_SIZE, NORM_SHARED_NCACHE | PRIV_RW_USER_RW);

	/* Initilaize GIC */
	zynqMP_r5_gic_initialize();

}