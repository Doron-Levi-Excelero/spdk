/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spdk/stdinc.h"

#include "spdk/bdev.h"
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/blob_bdev.h"
#include "spdk/blob.h"
#include "spdk/log.h"
#include "spdk/string.h"

/*
 * We'll use this struct to gather housekeeping hello_context to pass between
 * our events and callbacks.
 */
struct hello_context_t {
	struct spdk_blob_store *bs;
	struct spdk_blob *blob;
	spdk_blob_id blobid;
	struct spdk_io_channel *channel;
	uint8_t *read_buff;
	uint8_t *write_buff;
	uint64_t io_unit_size;
	int rc;
};


static void
base_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		   void *event_ctx)
{
	//AK: TODO - I'll have to handle it better in case there's a MD/DATA event in the operational system
	SPDK_WARNLOG("Unsupported bdev event: type %d\n", type);
}

static void
md_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		   void *event_ctx)
{
	//AK: TODO - I'll have to handle it better in case there's a MD/DATA event in the operational system
	SPDK_WARNLOG("Unsupported bdev event: type %d\n", type);
}

/*
 * Free up memory that we allocated.
 */
static void
hello_cleanup(struct nvidia_md_dev_context* md_ctx)
{
	//AK: TODO - cleanup buffs, if any...
	//spdk_free(md_ctx->read_buff);
	//spdk_free(md_ctx->write_buff);
	if (md_ctx->md_buff) {
		spdk_free(md_ctx->md_buff);
	}
	free(md_ctx);
}

/*
 * Callback routine for the blobstore unload.
 */
static void
unload_complete(void *cb_arg, int bserrno)
{
	struct hello_context_t *hello_context = cb_arg;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		SPDK_ERRLOG("Error %d unloading the bobstore\n", bserrno);
		hello_context->rc = bserrno;
	}

	spdk_app_stop(hello_context->rc);
}

/*
 * Unload the blobstore, cleaning up as needed.
 */
static void
unload_bs(struct nvidia_md_dev_context *md_ctx, char *msg, int bserrno)
{
	if (bserrno) {
		SPDK_ERRLOG("%s (err %d)\n", msg, bserrno);
		md_ctx->data_rc = bserrno;
	}
	if (md_ctx->data_bs) {
		if (md_ctx->data_channel) {
			spdk_bs_free_io_channel(md_ctx->data_channel);
		}
		spdk_bs_unload(md_ctx->data_bs, unload_complete, md_ctx);
	} else {
		spdk_app_stop(bserrno);
	}
}

static void
unload_md_bs(void* arg1, char *msg, int bserrno)
{	
	//AK: TODO - implement
	struct nvidia_md_dev_context *ctx = arg1;
	/*
	if (bserrno) {
		SPDK_ERRLOG("%s (err %d)\n", msg, bserrno);
		hello_context->rc = bserrno;
	}
	if (hello_context->bs) {
		if (hello_context->channel) {
			spdk_bs_free_io_channel(hello_context->channel);
		}
		spdk_bs_unload(hello_context->bs, unload_complete, hello_context);
	} else {
		spdk_app_stop(bserrno);
	}
	*/
}

/*
 * Callback routine for the deletion of a blob.
 */
static void
delete_complete(void *arg1, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx  = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in delete completion",
			  bserrno);
		return;
	}

	/* We're all done, we can unload the blobstore. */
	unload_bs(md_ctx, "", 0);
}

/*
 * Function for deleting a blob.
 */
static void
delete_blob(void *arg1, int bserrno)
{	
	struct nvidia_md_dev_context *md_ctx  = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in close completion",
			  bserrno);
		return;
	}

	spdk_bs_delete_blob(md_ctx->data_bs, md_ctx->data_blobid,
			    delete_complete, md_ctx);
}

/*
 * Callback function for reading a blob.
 */
static void
read_complete(void *arg1, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx  = arg1;
	int match_res = -1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in read completion",
			  bserrno);
		return;
	}

	/* Now let's make sure things match. */
	match_res = memcmp(md_ctx->data_write_buff, md_ctx->data_read_buff,
			   md_ctx->data_io_unit_size);
	if (match_res) {
		unload_bs(md_ctx, "Error in data compare", -1);
		return;
	} else {
		SPDK_NOTICELOG("read SUCCESS and data matches!\n");
	}

	/* Now let's close it and delete the blob in the callback. */
	spdk_blob_close(md_ctx->data_blob, delete_blob, md_ctx);
}

/*
 * Function for reading a blob.
 */
static void
read_blob(struct nvidia_md_dev_context *md_ctx)
{
	SPDK_NOTICELOG("entry\n");

	md_ctx->data_read_buff = spdk_malloc(md_ctx->data_io_unit_size,
					       0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
					       SPDK_MALLOC_DMA);
	if (md_ctx->data_read_buff == NULL) {
		unload_bs(md_ctx, "Error in memory allocation",
			  -ENOMEM);
		return;
	}

	/* Issue the read and compare the results in the callback. */
	spdk_blob_io_read(md_ctx->data_blob, md_ctx->data_channel,
			  md_ctx->data_read_buff, 0, 1, read_complete,
			  md_ctx);
}

/*
 * Callback function for writing a blob.
 */
static void
write_complete(void *arg1, int bserrno)
{	
	struct nvidia_md_dev_context *md_ctx = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in write completion",
			  bserrno);
		return;
	}

	/* Now let's read back what we wrote and make sure it matches. */
	read_blob(md_ctx);
}

/*
 * Function for writing to a blob.
 */
static void
blob_write(struct nvidia_md_dev_context *md_ctx)
{
	SPDK_NOTICELOG("entry\n");

	/*
	 * Buffers for data transfer need to be allocated via SPDK. We will
	 * transfer 1 io_unit of 4K aligned data at offset 0 in the blob.
	 */
	md_ctx->data_write_buff = spdk_malloc(md_ctx->data_io_unit_size,
						0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
						SPDK_MALLOC_DMA);
	if (md_ctx->data_write_buff == NULL) {
		unload_bs(md_ctx, "Error in allocating memory",
			  -ENOMEM);
		return;
	}
	memset(md_ctx->data_write_buff, 0x5a, md_ctx->data_io_unit_size);

	/* Now we have to allocate a channel. */
	md_ctx->data_channel = spdk_bs_alloc_io_channel(md_ctx->data_bs);
	if (md_ctx->data_channel == NULL) {
		unload_bs(md_ctx, "Error in allocating channel",
			  -ENOMEM);
		return;
	}

	/* Let's perform the write, 1 io_unit at offset 0. */
	spdk_blob_io_write(md_ctx->data_blob, md_ctx->data_channel,
			   md_ctx->data_write_buff,
			   0, 1, write_complete, md_ctx);
}

/*
 * Callback function for syncing metadata.
 */
static void
sync_complete(void *arg1, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in sync callback",
			  bserrno);
		return;
	}

	//AK: once the md blob was synced, we can start testing writes to the data blobstore
	/* Blob has been created & sized & MD sync'd, let's write to it. */
	blob_write(md_ctx);
}

static void
data_blob_open_complete(void *cb_arg, struct spdk_blob *blob, int bserrno)
{	
	struct nvidia_md_dev_context *md_ctx = cb_arg;
	uint64_t free = 0;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in open completion",
			  bserrno);
		return;
	}

	md_ctx->data_blob = blob;

	/*
	free = spdk_bs_free_cluster_count(md_ctx->data_bs);
	SPDK_NOTICELOG("blobstore has FREE clusters of %" PRIu64 "\n",
		       free);
	
	spdk_blob_resize(md_ctx->data_blob, free, data_blob_resize_complete, md_ctx);
	*/
	SPDK_NOTICELOG("exit\n");
}
/*
 * Callback function for creating a blob.
 */
static void
data_blob_create_complete(void *arg1, spdk_blob_id blobid, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_bs(md_ctx, "Error in blob create callback",
			  bserrno);
		return;
	}

	//md_ctx->data_blobid = blobid;
	SPDK_NOTICELOG("created new data blob with id %" PRIu64 "\n", blobid);

	/* We have to open the blob before we can do things like resize. */
	spdk_bs_open_blob(md_ctx->data_bs, blobid,
			  data_blob_open_complete, md_ctx);
	
	SPDK_NOTICELOG("exit\n");
}
/*
 * Function for creating a blob.
 */
static void
data_bs_create_blob(struct nvidia_md_dev_context *md_ctx)
{
	SPDK_NOTICELOG("entry\n");
	spdk_bs_create_blob(md_ctx->data_bs, data_blob_create_complete, md_ctx);
	SPDK_NOTICELOG("exit\n");
}

static void
md_blob_sync_complete(void *arg1, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_md_bs(md_ctx, "Error in sync callback",
			  bserrno);
		return;
	}

	//AK: TODO - now we can do stuff, I hope...
	data_bs_create_blob(md_ctx);

	SPDK_NOTICELOG("exit\n");
}

static void
md_resize_complete(void *cb_arg, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = cb_arg;
	struct spdk_bs_dev *data_bs_dev = NULL;
	uint64_t total = 0;

	if (bserrno) {
		unload_bs(md_ctx, "Error in blob resize", bserrno);
		return;
	}

	total = spdk_blob_get_num_clusters(md_ctx->md_blob);
	SPDK_NOTICELOG("resized md blob now has USED clusters of %" PRIu64 "\n",
		       total);

	/*
	 * Metadata is stored in volatile memory for performance
	 * reasons and therefore needs to be synchronized with
	 * non-volatile storage to make it persistent. This can be
	 * done manually, as shown here, or if not it will be done
	 * automatically when the blob is closed. It is always a
	 * good idea to sync after making metadata changes unless
	 * it has an unacceptable impact on application performance.
	 */
	spdk_blob_sync_md(md_ctx->md_blob, sync_complete, md_ctx);
}

/*
 * Callback function for initializing the blobstore.
 */
static void
data_bs_init_complete(void *cb_arg, struct spdk_blob_store *bs,
		 int bserrno)
{	
	struct nvidia_md_dev_context *md_ctx = cb_arg;
	
	SPDK_NOTICELOG("entry\n");
	if (bserrno) {		
		unload_bs(md_ctx, "Error initing the blobstore",
			  bserrno);		
		return;
	}

	md_ctx->data_bs = bs;
	/*
	 * We will use the io_unit size in allocating buffers, etc., later
	 * so we'll just save it in out context buffer here.
	 */
	md_ctx->data_io_unit_size = spdk_bs_get_io_unit_size(md_ctx->data_bs);

	SPDK_NOTICELOG("data blobstore: %p\n", md_ctx->data_bs);

	/*
	 * Metadata is stored in volatile memory for performance
	 * reasons and therefore needs to be synchronized with
	 * non-volatile storage to make it persistent. This can be
	 * done manually, as shown here, or if not it will be done
	 * automatically when the blob is closed. It is always a
	 * good idea to sync after making metadata changes unless
	 * it has an unacceptable impact on application performance.
	 */
	spdk_blob_sync_md(md_ctx->md_blob, md_blob_sync_complete, md_ctx);

	SPDK_NOTICELOG("exit\n");
}


static void
data_blob_resize_complete(void *cb_arg, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = cb_arg;
	struct spdk_bs_dev *data_bs_dev;
	uint64_t total = 0;

	SPDK_NOTICELOG("entry\n");

	if (bserrno) {
		unload_md_bs(md_ctx, "Error in md blob resize", bserrno);
		return;
	}

	total = spdk_blob_get_num_clusters(md_ctx->md_blob);
	SPDK_NOTICELOG("resized blob now has USED clusters of %" PRIu64 "\n",
		       total);

	int rc = spdk_bdev_create_bs_dev_ext(md_ctx->data_dev_name, base_bdev_event_cb, NULL, &data_bs_dev);
	if (rc != 0) {
		SPDK_ERRLOG("Could not create blob bdev, %s!!\n",
			    spdk_strerror(-rc));
		spdk_app_stop(-1);
		return;
	}	

	//AK: TODO - remove if not needed
	//SPDK_NOTICELOG("md blobstore after DATA_BDEV creation: %p\n", md_dev_ctx->md_bs);

	spdk_bs_init_with_md_dev(data_bs_dev, md_ctx, NULL, NULL, data_bs_init_complete, md_ctx);	

	SPDK_NOTICELOG("exit\n");
}


static void
md_blob_resize_complete(void *cb_arg, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = cb_arg;
	struct spdk_bs_dev *data_bs_dev;
	uint64_t total = 0;

	SPDK_NOTICELOG("entry\n");

	if (bserrno) {
		unload_md_bs(md_ctx, "Error in md blob resize", bserrno);
		return;
	}

	total = spdk_blob_get_num_clusters(md_ctx->md_blob);
	SPDK_NOTICELOG("resized blob now has USED clusters of %" PRIu64 "\n",
		       total);

	int rc = spdk_bdev_create_bs_dev_ext(md_ctx->data_dev_name, base_bdev_event_cb, NULL, &data_bs_dev);
	if (rc != 0) {
		SPDK_ERRLOG("Could not create blob bdev, %s!!\n",
			    spdk_strerror(-rc));
		spdk_app_stop(-1);
		return;
	}	

	//AK: TODO - remove if not needed
	//SPDK_NOTICELOG("md blobstore after DATA_BDEV creation: %p\n", md_dev_ctx->md_bs);

	spdk_bs_init_with_md_dev(data_bs_dev, md_ctx, NULL, NULL, data_bs_init_complete, md_ctx);	

	SPDK_NOTICELOG("exit\n");
}


static void
md_blob_open_complete(void *cb_arg, struct spdk_blob *blob, int bserrno)
{
	struct nvidia_md_dev_context *md_ctx = cb_arg;
	uint64_t free = 0;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_md_bs(md_ctx, "Error in open completion",
			  bserrno);
		return;
	}


	md_ctx->md_blob = blob;
	free = spdk_bs_free_cluster_count(md_ctx->md_bs);
	SPDK_NOTICELOG("md blobstore has FREE clusters of %" PRIu64 "\n",
		       free);

	/*
	 * Before we can use our new blob, we have to resize it
	 * as the initial size is 0. For this example we'll use the
	 * full size of the blobstore but it would be expected that
	 * there'd usually be many blobs of various sizes. The resize
	 * unit is a cluster.
	 */
	spdk_blob_resize(md_ctx->md_blob, free, md_blob_resize_complete, md_ctx);
}



static void
md_blob_create_complete(void *arg1, spdk_blob_id blobid, int bserrno)
{
	struct nvidia_md_dev_context *md_dev_ctx = arg1;

	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		unload_md_bs(md_dev_ctx, "Error in blob create callback",
			  bserrno);
		return;
	}

	md_dev_ctx->md_blobid = blobid;
	SPDK_NOTICELOG("new md blob id %" PRIu64 "\n", md_dev_ctx->md_blobid);

	/* We have to open the blob before we can do things like resize. */
	spdk_bs_open_blob(md_dev_ctx->md_bs, md_dev_ctx->md_blobid,
			  md_blob_open_complete, md_dev_ctx);
}







//AK: TODO - Might not be required
static void
md_bdev_dummy_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, 
			void *event_ctx)
{

}

static int 
create_md_bdev(const char *bdev_name, struct nvidia_md_dev_context *ctx)
{
	uint32_t blk_size, buf_align;
	uint64_t blk_num;
	int rc = 0;
	ctx->md_bdev = NULL;
	ctx->md_bdev_desc = NULL;
	
	/*
	 * There can be many bdevs configured, but this application will only use
	 * the one input by the user at runtime.
	 *
	 * Open the bdev by calling spdk_bdev_open_ext() with its name.
	 * The function will return a descriptor
	 */
	SPDK_NOTICELOG("Opening md_bdev %s\n", bdev_name);


	rc = spdk_bdev_open_ext(bdev_name, true, md_bdev_event_cb, NULL,
				&ctx->md_bdev_desc);
	if (rc) {
		SPDK_ERRLOG("Could not open bdev: %s\n", bdev_name);
		return rc;
	}

	/* A bdev pointer is valid while the bdev is opened. */
	ctx->md_bdev = spdk_bdev_desc_get_bdev(ctx->md_bdev_desc);


	SPDK_NOTICELOG("Opening io channel on bdev_name\n");
	/* Open I/O channel */
	ctx->md_bdev_io_channel = spdk_bdev_get_io_channel(ctx->md_bdev_desc);
	if (ctx->md_bdev_io_channel == NULL) {
		SPDK_ERRLOG("Could not create bdev I/O channel!!\n");
		spdk_bdev_close(ctx->md_bdev_desc);
		return -ENOMEM;
	}
	
	blk_size = spdk_bdev_get_block_size(ctx->md_bdev);
	buf_align = spdk_bdev_get_buf_align(ctx->md_bdev);
	ctx->md_buff = spdk_dma_zmalloc(blk_size, buf_align, NULL);
	if (!ctx->md_buff) {
		SPDK_ERRLOG("Failed to allocate buffer\n");
		spdk_put_io_channel(ctx->md_bdev_io_channel);
		spdk_bdev_close(ctx->md_bdev_desc);
		return -ENOMEM;
	}

	blk_num  = spdk_bdev_get_num_blocks(ctx->md_bdev);
	SPDK_NOTICELOG("%lu blocks on %s\n", blk_num, bdev_name);

	return 0;
}

#define MD_BDEV 	"Malloc0_MD"
#define DATA_BDEV 	"Malloc0_Data"

static void
bs_init_md_bs_bdev_complete(void *cb_arg, struct spdk_blob_store *bs,
		 int bserrno)
{
	struct nvidia_md_dev_context *md_dev_ctx = cb_arg;	

	SPDK_NOTICELOG("Start\n");
	
	md_dev_ctx->md_bs = bs;
	//AK: TODO - remove if not needed
	md_dev_ctx->md_io_unit_size = spdk_bs_get_io_unit_size(md_dev_ctx->md_bs);

	//AK: TODO - remove if not needed
	//SPDK_NOTICELOG("md blobstore: %p\n", md_dev_ctx->md_bs);

	//Create the blob where the MD will be written on the md_bs_dev (this is actually the second cluster after the MD cluster on the ms_bs_dev)
	spdk_bs_create_blob(md_dev_ctx->md_bs, md_blob_create_complete, md_dev_ctx);

	//AK: TODO - remove if not needed
	//SPDK_NOTICELOG("md blobstore after creating one blob: %p\n", md_dev_ctx->md_bs);
	
	SPDK_NOTICELOG("End\n");
	/*
	SPDK_NOTICELOG("entry\n");
	if (bserrno) {
		//AK: TODO - might need to close md_dev_ctx->data_bs->dev in case of any failure here
		unload_md_bs(md_dev_ctx, "Error initing the md blobstore",
			  bserrno);		
		return;
	}
	return;
	*/
}

//AK: TODO - remove if not needed
/*
static void
nvidia_create_md_bdev(const char* md_bs_name, struct nvidia_md_dev_context *md_ctx)
{
	//AK: TODO - create a bdev and zero it...
}
*/

static int
nvidia_create_md_bs_bdev(const char* md_bs_name, struct nvidia_md_dev_context *md_ctx)
{
	//struct hello_context_t *hello_context = arg1;
	struct spdk_bs_dev *md_bs_dev = NULL;

	int rc = spdk_bdev_create_bs_dev_ext(md_bs_name, md_bdev_event_cb, NULL, &md_bs_dev);
	if (rc != 0) {
		SPDK_ERRLOG("Could not create md_bs_dev, %s!!\n",
			    spdk_strerror(-rc));
		spdk_app_stop(-1);
		return rc;
	}
	spdk_bs_init(md_bs_dev, NULL, bs_init_md_bs_bdev_complete, md_ctx);
	
	//AK: TODO - consider the error value and return it
	return 0;
}
//AK: TODO - This was changed to be able to init a bs with an additional md bdev
/*
 * Our initial event that kicks off everything from main().
 */
static void 
hello_start_nvidia(void *arg1)
{
	struct nvidia_md_dev_context *md_ctx = arg1;
	struct spdk_bs_dev *bs_dev = NULL;
	
	struct spdk_bdev_desc	*md_desc = NULL;
	struct spdk_bdev_desc	*read_only_desc = NULL;
	
	int rc;

	SPDK_NOTICELOG("hello_start_nvidia entry\n");

	/*
	 * In this example, use our malloc (RAM) disk configured via
	 * hello_blob.json that was passed in when we started the
	 * SPDK app framework.
	 *
	 * spdk_bs_init() requires us to fill out the structure
	 * spdk_bs_dev with a set of callbacks. These callbacks
	 * implement read, write, and other operations on the
	 * underlying disks. As a convenience, a utility function
	 * is provided that creates an spdk_bs_dev that implements
	 * all of the callbacks by forwarding the I/O to the
	 * SPDK bdev layer. Other helper functions are also
	 * available in the blob lib in blob_bdev.c that simply
	 * make it easier to layer blobstore on top of a bdev.
	 * However blobstore can be more tightly integrated into
	 * any lower layer, such as NVMe for example.
	 */


	//AK: option 1, us a simple bdev
	//rc = nvidia_create_bdev(struct nvidia_md_dev_context *ctx)
	//AK: option 2, us a bs_bdev on the md device:
	nvidia_create_md_bs_bdev(md_ctx->md_dev_name, md_ctx);

	SPDK_NOTICELOG("Finished NVIDIA hello blob\n");
}

int
main(int argc, char **argv)
{
	struct spdk_app_opts opts = {};
	int rc = 0;

	//AK: TODO - make this dynamically allocated and free in due time	
	struct nvidia_md_dev_context* md_ctx = NULL;

	SPDK_NOTICELOG("entry\n");

	/* Set default values in opts structure. */
	spdk_app_opts_init(&opts, sizeof(opts));

	/*
	 * Setup a few specifics before we init, for most SPDK cmd line
	 * apps, the config file will be passed in as an arg but to make
	 * this example super simple we just hardcode it. We also need to
	 * specify a name for the app.
	 */
	opts.name = "hello_blob_nvidia";
	opts.json_config_file = argv[1];

	md_ctx = calloc(1, sizeof(*md_ctx));
	if (md_ctx != NULL) {	
		memset(md_ctx, 0, sizeof(*md_ctx));
		md_ctx->md_dev_name		= MD_BDEV;
		md_ctx->data_dev_name 	= DATA_BDEV;

		//The buffer md pages are read to.
		//AK: TODO - use SPDK_BS_PAGE_SIZE instead of hardcoded 0x1000 values (for len/alignment)
		md_ctx->md_buff = spdk_malloc(0x1000, 0x1000, NULL, SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
		if (md_ctx->md_buff){
			rc = spdk_app_start(&opts, hello_start_nvidia, md_ctx);
			if (rc) {
				SPDK_NOTICELOG("ERROR!\n");
			} else {
				SPDK_NOTICELOG("SUCCESS!\n");
			}
		}
		/* Free up memory that we allocated */
		hello_cleanup(md_ctx);
	} else {
		SPDK_ERRLOG("Could not alloc hello_context struct!!\n");
		rc = -ENOMEM;
	}

	/* Gracefully close out all of the SPDK subsystems. */
	spdk_app_fini();
	return rc;
}
