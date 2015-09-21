/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
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

#include "nvme/nvme_internal.h"

#include "CUnit/Basic.h"

#include "nvme/nvme_ns_cmd.c"

char outbuf[OUTBUF_SIZE];

struct nvme_request *g_request = NULL;

uint64_t nvme_vtophys(void *buf)
{
	return (uintptr_t)buf;
}

int
nvme_ctrlr_hw_reset(struct nvme_controller *ctrlr)
{
	return 0;
}

int
nvme_ctrlr_construct(struct nvme_controller *ctrlr, void *devhandle)
{
	return 0;
}

void
nvme_ctrlr_destruct(struct nvme_controller *ctrlr)
{
}

int
nvme_ctrlr_start(struct nvme_controller *ctrlr)
{
	return 0;
}

uint32_t
nvme_ns_get_sector_size(struct nvme_namespace *ns)
{
	return ns->sector_size;
}

uint32_t
nvme_ns_get_max_io_xfer_size(struct nvme_namespace *ns)
{
	return ns->ctrlr->max_xfer_size;
}

void
nvme_ctrlr_submit_io_request(struct nvme_controller *ctrlr,
			     struct nvme_request *req)
{
	g_request = req;
}

void
prepare_for_test(struct nvme_namespace *ns, struct nvme_controller *ctrlr,
		 uint32_t sector_size, uint32_t max_xfer_size,
		 uint32_t stripe_size)
{
	ctrlr->max_xfer_size = max_xfer_size;
	ns->ctrlr = ctrlr;
	ns->sector_size = sector_size;
	ns->stripe_size = stripe_size;
	ns->sectors_per_max_io = nvme_ns_get_max_io_xfer_size(ns) / ns->sector_size;
	ns->sectors_per_stripe = ns->stripe_size / ns->sector_size;

	g_request = NULL;
}

void
split_test(void)
{
	struct nvme_namespace	ns;
	struct nvme_controller	ctrlr;
	void			*payload;
	uint64_t		lba;
	uint32_t		lba_count;
	int			rc;

	prepare_for_test(&ns, &ctrlr, 512, 128 * 1024, 0);
	payload = 0x0;
	lba = 0;
	lba_count = 1;

	rc = nvme_ns_cmd_read(&ns, payload, lba, lba_count, NULL, NULL);

	CU_ASSERT(rc == 0);
	CU_ASSERT(g_request != NULL);
	CU_ASSERT(g_request->num_children == 0);

	nvme_free_request(g_request);
}

void
split_test2(void)
{
	struct nvme_namespace	ns;
	struct nvme_controller	ctrlr;
	struct nvme_request	*child;
	void			*payload;
	uint64_t		lba;
	uint32_t		lba_count;
	int			rc;

	prepare_for_test(&ns, &ctrlr, 512, 128 * 1024, 0);
	payload = malloc(256 * 1024);
	lba = 0;
	lba_count = (256 * 1024) / 512;

	rc = nvme_ns_cmd_read(&ns, payload, lba, lba_count, NULL, NULL);

	CU_ASSERT(rc == 0);
	CU_ASSERT(g_request != NULL);
	CU_ASSERT(g_request->num_children == 2);

	child = TAILQ_FIRST(&g_request->children);
	TAILQ_REMOVE(&g_request->children, child, child_tailq);
	CU_ASSERT(child->num_children == 0);
	CU_ASSERT(child->payload_size == 128 * 1024);

	child = TAILQ_FIRST(&g_request->children);
	TAILQ_REMOVE(&g_request->children, child, child_tailq);
	CU_ASSERT(child->num_children == 0);
	CU_ASSERT(child->payload_size == 128 * 1024);

	CU_ASSERT(TAILQ_EMPTY(&g_request->children));

	free(payload);
	nvme_free_request(g_request);
}

void
test_nvme_ns_cmd_flush(void)
{
	struct nvme_namespace	ns;
	struct nvme_controller	ctrlr;
	nvme_cb_fn_t		cb_fn = NULL;
	void			*cb_arg = NULL;

	prepare_for_test(&ns, &ctrlr, 512, 128 * 1024, 0);

	nvme_ns_cmd_flush(&ns, cb_fn, cb_arg);
	CU_ASSERT(g_request->cmd.opc == NVME_OPC_FLUSH);
	CU_ASSERT(g_request->cmd.nsid == ns.id);

	nvme_free_request(g_request);
}

void
test_nvme_ns_cmd_deallocate(void)
{
	struct nvme_namespace	ns;
	struct nvme_controller	ctrlr;
	nvme_cb_fn_t		cb_fn = NULL;
	void			*cb_arg = NULL;
	uint8_t			num_ranges = 1;
	void			*payload = NULL;
	int			rc = 0;

	prepare_for_test(&ns, &ctrlr, 512, 128 * 1024, 0);
	payload = malloc(num_ranges * sizeof(struct nvme_dsm_range));

	nvme_ns_cmd_deallocate(&ns, payload, num_ranges, cb_fn, cb_arg);
	CU_ASSERT(g_request->cmd.opc == NVME_OPC_DATASET_MANAGEMENT);
	CU_ASSERT(g_request->cmd.nsid == ns.id);
	CU_ASSERT(g_request->cmd.cdw10 == num_ranges - 1);
	CU_ASSERT(g_request->cmd.cdw11 == NVME_DSM_ATTR_DEALLOCATE);
	free(payload);
	nvme_free_request(g_request);

	payload = NULL;
	num_ranges = 0;
	rc = nvme_ns_cmd_deallocate(&ns, payload, num_ranges, cb_fn, cb_arg);
	CU_ASSERT(rc != 0);
}

int main(int argc, char **argv)
{
	CU_pSuite	suite = NULL;
	unsigned int	num_failures;

	if (CU_initialize_registry() != CUE_SUCCESS) {
		return CU_get_error();
	}

	suite = CU_add_suite("nvme_ns_cmd", NULL, NULL);
	if (suite == NULL) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if (
		CU_add_test(suite, "split_test", split_test) == NULL
		|| CU_add_test(suite, "split_test2", split_test2) == NULL
		|| CU_add_test(suite, "nvme_ns_cmd_flush testing", test_nvme_ns_cmd_flush) == NULL
		|| CU_add_test(suite, "nvme_ns_cmd_deallocate testing", test_nvme_ns_cmd_deallocate) == NULL
	) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	num_failures = CU_get_number_of_failures();
	CU_cleanup_registry();
	return num_failures;
}
