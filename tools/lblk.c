#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libxnvme.h>
#include <libxnvmec.h>

// TODO: only show namespaces of logical block type
static int
sub_enumerate(struct xnvmec *cli)
{
	struct xnvme_enumeration *listing = NULL;
	int err;

	xnvmec_pinf("xnvme_enumerate()");

	err = xnvme_enumerate(&listing, cli->args.sys_uri, cli->args.flags);
	if (err) {
		xnvmec_perr("xnvme_enumerate()", err);
		goto exit;
	}

	xnvme_enumeration_pr(listing, XNVME_PR_DEF);

exit:
	free(listing);

	return err;
}

static int
sub_info(struct xnvmec *cli)
{
	struct xnvme_dev *dev = cli->args.dev;

	xnvme_dev_pr(dev, XNVME_PR_DEF);

	return 0;
}

static inline int
sub_idfy(struct xnvmec *cli)
{
	struct xnvme_dev *dev = cli->args.dev;
	struct xnvme_spec_idfy *idfy = NULL;
	uint32_t nsid = xnvme_dev_get_nsid(cli->args.dev);
	struct xnvme_req req = { 0 };
	int err;

	xnvmec_pinf("xnvme_cmd_idfy: {nsid: 0x%x}", nsid);

	idfy = xnvme_buf_alloc(dev, sizeof(*idfy), NULL);
	if (!idfy) {
		err = -errno;
		xnvmec_perr("xnvme_buf_alloc()", err);
		goto exit;
	}

	err = xnvme_cmd_idfy_ns(dev, nsid, idfy, &req);
	if (err || xnvme_req_cpl_status(&req)) {
		xnvmec_perr("xnvme_cmd_idfy()", err);
		xnvme_req_pr(&req, XNVME_PR_DEF);
		err = err ? err : -EIO;
		goto exit;
	}

	xnvme_spec_idfy_ns_pr(&idfy->ns, XNVME_PR_DEF);

	if (cli->args.data_output) {
		xnvmec_pinf("Dumping to: '%s'", cli->args.data_output);
		err = xnvmec_buf_to_file((char *) idfy, sizeof(*idfy),
					 cli->args.data_output);
		if (err) {
			xnvmec_perr("xnvmec_buf_to_file()", err);
		}
	}

exit:
	xnvme_buf_free(dev, idfy);

	return err;
}

static int
sub_read(struct xnvmec *cli)
{
	struct xnvme_dev *dev = cli->args.dev;
	const struct xnvme_geo *geo = cli->args.geo;
	const uint64_t slba = cli->args.slba;
	const size_t nlb = cli->args.nlb;
	uint8_t nsid = cli->args.nsid;

	void *dbuf = NULL, *mbuf = NULL;
	size_t dbuf_nbytes, mbuf_nbytes;
	struct xnvme_req req = { 0 };
	int err;

	if (!cli->given[XNVMEC_OPT_NSID]) {
		nsid = xnvme_dev_get_nsid(cli->args.dev);
	}

	dbuf_nbytes = (nlb + 1) * geo->lba_nbytes;
	mbuf_nbytes = geo->lba_extended ? 0 : (nlb + 1) * geo->nbytes_oob;

	xnvmec_pinf("Reading nsid: 0x%x, slba: 0x%016lx, nlb: %zu",
		    nsid, slba, nlb);

	xnvmec_pinf("Alloc/clear dbuf, dbuf_nbytes: %zu", dbuf_nbytes);
	dbuf = xnvme_buf_alloc(dev, dbuf_nbytes, NULL);
	if (!dbuf) {
		err = -errno;
		xnvmec_perr("xnvme_buf_alloc()", err);
		goto exit;
	}
	memset(dbuf, 0, dbuf_nbytes);

	if (mbuf_nbytes) {
		xnvmec_pinf("Alloc/clear mbuf, mbuf_nbytes: %zu", mbuf_nbytes);
		mbuf = xnvme_buf_alloc(dev, mbuf_nbytes, NULL);
		if (!mbuf) {
			err = -errno;
			xnvmec_perr("xnvme_buf_alloc()", err);
			goto exit;
		}
		memset(mbuf, 0, mbuf_nbytes);
	}

	xnvmec_pinf("Sending the command...");
	err = xnvme_cmd_read(dev, nsid, slba, nlb, dbuf, mbuf, 0x0, &req);
	if (err || xnvme_req_cpl_status(&req)) {
		xnvmec_perr("xnvme_cmd_read()", err);
		xnvme_req_pr(&req, XNVME_PR_DEF);
		err = err ? err : -EIO;
		goto exit;
	}

	if (cli->args.data_output) {
		xnvmec_pinf("dumping to: '%s'", cli->args.data_output);
		err = xnvmec_buf_to_file(dbuf, dbuf_nbytes,
					 cli->args.data_output);
		if (err) {
			xnvmec_perr("xnvmec_buf_to_file()", err);
		}
	}

exit:
	xnvme_buf_free(dev, dbuf);
	xnvme_buf_free(dev, mbuf);

	return err;
}

static int
sub_write(struct xnvmec *cli)
{
	struct xnvme_dev *dev = cli->args.dev;
	const struct xnvme_geo *geo = cli->args.geo;
	const uint64_t slba = cli->args.slba;
	const size_t nlb = cli->args.nlb;
	uint32_t nsid = cli->args.nsid;

	void *dbuf = NULL, *mbuf = NULL;
	size_t dbuf_nbytes, mbuf_nbytes;
	struct xnvme_req req = { 0 };
	int err;

	if (!cli->given[XNVMEC_OPT_NSID]) {
		nsid = xnvme_dev_get_nsid(cli->args.dev);
	}

	dbuf_nbytes = (nlb + 1) * geo->lba_nbytes;
	mbuf_nbytes = geo->lba_extended ? 0 : (nlb + 1) * geo->nbytes_oob;

	xnvmec_pinf("Writing nsid: 0x%x, slba: 0x%016lx, nlb: %zu",
		    nsid, slba, nlb);

	xnvmec_pinf("Alloc/fill dbuf, dbuf_nbytes: %zu", dbuf_nbytes);
	dbuf = xnvme_buf_alloc(dev, dbuf_nbytes, NULL);
	if (!dbuf) {
		err = -errno;
		xnvmec_perr("xnvme_buf_alloc()", err);
		goto exit;
	}
	err = xnvmec_buf_fill(dbuf, dbuf_nbytes, cli->args.data_input ? cli->args.data_input : "anum");
	if (err) {
		xnvmec_perr("xnvmec_buf_fill()", err);
		goto exit;
	}

	if (mbuf_nbytes) {
		xnvmec_pinf("Alloc/fill mbuf, mbuf_nbytes: %zu", mbuf_nbytes);
		mbuf = xnvme_buf_alloc(dev, mbuf_nbytes, NULL);
		if (!mbuf) {
			err = -errno;
			xnvmec_perr("xnvme_buf_alloc()", err);
			goto exit;
		}
		err = xnvmec_buf_fill(mbuf, mbuf_nbytes, "anum");
		if (err) {
			xnvmec_perr("xnvmec_buf_fill()", err);
			goto exit;
		}
	}

	xnvmec_pinf("Sending the command...");
	err = xnvme_cmd_write(dev, nsid, slba, nlb, dbuf, mbuf, 0x0, &req);
	if (err || xnvme_req_cpl_status(&req)) {
		xnvmec_perr("xnvme_cmd_write()", err);
		xnvme_req_pr(&req, XNVME_PR_DEF);
		err = err ? err : -EIO;
		goto exit;
	}

exit:
	xnvme_buf_free(dev, dbuf);
	xnvme_buf_free(dev, mbuf);

	return err;
}

static int
sub_write_zeroes(struct xnvmec *XNVME_UNUSED(cli))
{
	xnvmec_pinf("Not implemented");

	return -1;
}

static int
sub_write_uncor(struct xnvmec *XNVME_UNUSED(cli))
{
	xnvmec_pinf("Not implemented");

	return -1;
}

//
// Command-Line Interface (CLI) definition
//
static struct xnvmec_sub subs[] = {
	{
		"enum", "Enumerate Logical Block Namespaces on the system",
		"Enumerate Logical Block Namespaces on the system", sub_enumerate, {
			{XNVMEC_OPT_SYS_URI, XNVMEC_LOPT},
			{XNVMEC_OPT_FLAGS, XNVMEC_LOPT},
		}
	},
	{
		"info", "Retrieve derived information for the given URI",
		"Retrieve derived information for the given URI", sub_info, {
			{XNVMEC_OPT_URI, XNVMEC_POSA},
		}
	},
	{
		"idfy", "Identify the namespace for the given URI",
		"Identify the namespace for the given URI", sub_idfy, {
			{XNVMEC_OPT_URI, XNVMEC_POSA},
			{XNVMEC_OPT_NSID, XNVMEC_LOPT},
			{XNVMEC_OPT_DATA_OUTPUT, XNVMEC_LOPT},
		}
	},
	{
		"read", "Read data and optionally metadata",
		"Read data and optionally metadata", sub_read, {
			{XNVMEC_OPT_URI, XNVMEC_POSA},
			{XNVMEC_OPT_SLBA, XNVMEC_LREQ},
			{XNVMEC_OPT_NLB, XNVMEC_LREQ},
			{XNVMEC_OPT_NSID, XNVMEC_LOPT},
			{XNVMEC_OPT_DATA_OUTPUT, XNVMEC_LOPT},
			{XNVMEC_OPT_META_OUTPUT, XNVMEC_LOPT},
		}
	},
	{
		"write", "Writes data and optionally metadata",
		"Writes data and optionally metadata", sub_write, {
			{XNVMEC_OPT_URI, XNVMEC_POSA},
			{XNVMEC_OPT_SLBA, XNVMEC_LREQ},
			{XNVMEC_OPT_NLB, XNVMEC_LREQ},
			{XNVMEC_OPT_NSID, XNVMEC_LOPT},
			{XNVMEC_OPT_DATA_INPUT, XNVMEC_LOPT},
			{XNVMEC_OPT_META_INPUT, XNVMEC_LOPT},
		}
	},
	{
		"write-zeros", "Set a range of logical blocks to zero",
		"Set a range of logical blocks to zero", sub_write_zeroes, {
			{XNVMEC_OPT_URI, XNVMEC_POSA},
			{XNVMEC_OPT_SLBA, XNVMEC_LREQ},
			{XNVMEC_OPT_NLB, XNVMEC_LREQ},
			{XNVMEC_OPT_NSID, XNVMEC_LOPT},
			{XNVMEC_OPT_DATA_INPUT, XNVMEC_LOPT},
			{XNVMEC_OPT_META_INPUT, XNVMEC_LOPT},
		}
	},
	{
		"write-uncor", "Mark a range of logical blocks as invalid",
		"Mark a range of logical blocks as invalid", sub_write_uncor, {
			{XNVMEC_OPT_URI, XNVMEC_POSA},
			{XNVMEC_OPT_SLBA, XNVMEC_LREQ},
			{XNVMEC_OPT_NLB, XNVMEC_LREQ},
			{XNVMEC_OPT_NSID, XNVMEC_LOPT},
			{XNVMEC_OPT_DATA_INPUT, XNVMEC_LOPT},
			{XNVMEC_OPT_META_INPUT, XNVMEC_LOPT},
		}
	},
};

static struct xnvmec cli = {
	.title = "Logical Block Namespace Utility",
	.descr_short = "Logical Block Namespace Utility",
	.descr_long = "",
	.subs = subs,
	.nsubs = sizeof subs / sizeof(*subs),
};

int
main(int argc, char **argv)
{
	return xnvmec(&cli, argc, argv, XNVMEC_INIT_DEV_OPEN);
}
