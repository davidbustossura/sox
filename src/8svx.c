/*
 * Amiga 8SVX format handler: W V Neisius, February 1992
 */

#include <math.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>	/* For SEEK_* defines if not found in stdio */
#endif

#include "st.h"

/* Private data used by writer */
typedef struct svxpriv {
	ULONG nsamples;
	FILE *ch[4];
}*svx_t;

static void svxwriteheader(ft_t, LONG);

/*======================================================================*/
/*                         8SVXSTARTREAD                                */
/*======================================================================*/

int st_svxstartread(ft)
ft_t ft;
{
	svx_t p = (svx_t ) ft->priv;

	char buf[12];
	char *chunk_buf;
 
	ULONG totalsize;
	ULONG chunksize;

	ULONG channels;
	unsigned short rate;
	int i;

	ULONG chan1_pos;

	if (! ft->seekable)
	{
		st_fail_errno(ft,ST_EINVAL,"8svx input file must be a file, not a pipe");
		return (ST_EOF);
	}
	/* 8svx is in big endian format. Swap whats
	 * read in on little endian machines.
	 */
	if (ST_IS_LITTLEENDIAN)
	{
		ft->swap = ft->swap ? 0 : 1;
	}

	rate = 0;
	channels = 1;

	/* read FORM chunk */
	if (st_reads(ft, buf, 4) == ST_EOF || strncmp(buf, "FORM", 4) != 0)
	{
		st_fail_errno(ft, ST_EHDR, "Header did not begin with magic word 'FORM'");
		return(ST_EOF);
	}
	st_readdw(ft, &totalsize);
	if (st_reads(ft, buf, 4) == ST_EOF || strncmp(buf, "8SVX", 4) != 0)
	{
		st_fail_errno(ft, ST_EHDR, "'FORM' chunk does not specify '8SVX' as type");
		return(ST_EOF);
	}

	/* read chunks until 'BODY' (or end) */
	while (st_reads(ft, buf, 4) == ST_SUCCESS && strncmp(buf,"BODY",4) != 0) {
		if (strncmp(buf,"VHDR",4) == 0) {
			st_readdw(ft, &chunksize);
			if (chunksize != 20)
			{
				st_fail_errno(ft, ST_EHDR, "VHDR chunk has bad size");
				return(ST_EOF);
			}
			fseek(ft->fp,12,SEEK_CUR);
			st_readw(ft, &rate);
			fseek(ft->fp,1,SEEK_CUR);
			fread(buf,1,1,ft->fp);
			if (buf[0] != 0)
			{
				st_fail_errno(ft, ST_EFMT, "Unsupported data compression");
				return(ST_EOF);
			}
			fseek(ft->fp,4,SEEK_CUR);
			continue;
		}

		if (strncmp(buf,"ANNO",4) == 0) {
			st_readdw(ft, &chunksize);
			if (chunksize & 1)
				chunksize++;
			chunk_buf = (char *) malloc(chunksize + 2);
			if (chunk_buf == 0)
			{
			    st_fail_errno(ft, ST_ENOMEM, "Unable to alloc memory");
			    return(ST_EOF);
			}
			if (fread(chunk_buf,1,(size_t)chunksize,ft->fp) 
					!= chunksize)
			{
				st_fail_errno(ft, ST_EHDR, "Couldn't read all of header");
				return(ST_EOF);
			}
			chunk_buf[chunksize] = '\0';
			st_report("%s",chunk_buf);
			free(chunk_buf);

			continue;
		}

		if (strncmp(buf,"NAME",4) == 0) {
			st_readdw(ft, &chunksize);
			if (chunksize & 1)
				chunksize++;
			chunk_buf = (char *) malloc(chunksize + 1);
			if (chunk_buf == 0)
			{
			    st_fail_errno(ft, ST_ENOMEM, "Unable to alloc memory");
			    return(ST_EOF);
			}
			if (fread (chunk_buf,1,(size_t)chunksize,ft->fp) 
					!= chunksize)
			{
				st_fail_errno(ft, ST_EHDR, "Couldn't read all of header");
				return(ST_EOF);
			}
			chunk_buf[chunksize] = '\0';
			st_report("%s",chunk_buf);
			free(chunk_buf);

			continue;
		}

		if (strncmp(buf,"CHAN",4) == 0) {
			st_readdw(ft, &chunksize);
			if (chunksize != 4) 
			{
				st_fail_errno(ft, ST_EHDR, "Couldn't read all of header");
				return(ST_EOF);
			}
			st_readdw(ft, &channels);
			channels = (channels & 0x01) + 
					((channels & 0x02) >> 1) +
				   	((channels & 0x04) >> 2) + 
					((channels & 0x08) >> 3);

			continue;
		}

		/* some other kind of chunk */
		st_readdw(ft, &chunksize);
		if (chunksize & 1)
			chunksize++;
		fseek(ft->fp,chunksize,SEEK_CUR);
		continue;

	}

	if (rate == 0)
	{
		st_fail_errno(ft, ST_ERATE, "Invalid sample rate");
		return(ST_EOF);
	}
	if (strncmp(buf,"BODY",4) != 0)
	{
		st_fail_errno(ft, ST_EHDR, "BODY chunk not found");
		return(ST_EOF);
	}
	st_readdw(ft, &(p->nsamples));

	ft->info.channels = channels;
	ft->info.rate = rate;
	ft->info.encoding = ST_ENCODING_SIGN2;
	ft->info.size = ST_SIZE_BYTE;

	/* open files to channels */
	p->ch[0] = ft->fp;
	chan1_pos = ftell(p->ch[0]);

	for (i = 1; i < channels; i++) {
		if ((p->ch[i] = fopen(ft->filename, READBINARY)) == NULL)
		{
			st_fail_errno(ft,errno,"Can't open channel file '%s'",
				ft->filename);
			return(ST_EOF);
		}

		/* position channel files */
		if (fseek(p->ch[i],chan1_pos,SEEK_SET))
		{
		    st_fail_errno (ft,errno,"Can't position channel %d",i);
		    return(ST_EOF);
		}
		if (fseek(p->ch[i],p->nsamples/channels*i,SEEK_CUR))
		{
		    st_fail_errno (ft,errno,"Can't seek channel %d",i);
		    return(ST_EOF);
		}
	}
	return(ST_SUCCESS);
}

/*======================================================================*/
/*                         8SVXREAD                                     */
/*======================================================================*/
LONG st_svxread(ft, buf, nsamp) 
ft_t ft;
LONG *buf, nsamp;
{
	unsigned char datum;
	int done = 0;
	int i;

	svx_t p = (svx_t ) ft->priv;

	while (done < nsamp) {
		for (i = 0; i < ft->info.channels; i++) {
		    	/* FIXME: don't pass FILE pointers! */
		    	datum = getc(p->ch[i]);
			if (feof(p->ch[i]))
				return done;
			/* scale signed up to long's range */
			*buf++ = LEFT(datum, 24);
		}
		done += ft->info.channels;
	}
	return done;
}

/*======================================================================*/
/*                         8SVXSTOPREAD                                 */
/*======================================================================*/
int st_svxstopread(ft)
ft_t ft;
{
	int i;

	svx_t p = (svx_t ) ft->priv;

	/* close channel files */
	for (i = 1; i < ft->info.channels; i++) {
		fclose (p->ch[i]);
	}
	return(ST_SUCCESS);
}

/*======================================================================*/
/*                         8SVXSTARTWRITE                               */
/*======================================================================*/
int st_svxstartwrite(ft)
ft_t ft;
{
	svx_t p = (svx_t ) ft->priv;
	int i;

	/* 8svx is in big endian format.  Swaps wahst
	 * read in on little endian machines.
	 */
	if (ST_IS_LITTLEENDIAN)
	{
		ft->swap = ft->swap ? 0 : 1;
	}

	/* open channel output files */
	p->ch[0] = ft->fp;
	for (i = 1; i < ft->info.channels; i++) {
		if ((p->ch[i] = tmpfile()) == NULL)
		{
			st_fail_errno(ft,errno,"Can't open channel output file");
			return(ST_EOF);
		}
	}

	/* write header (channel 0) */
	ft->info.encoding = ST_ENCODING_SIGN2;
	ft->info.size = ST_SIZE_BYTE;

	p->nsamples = 0;
	svxwriteheader(ft, p->nsamples);
	return(ST_SUCCESS);
}

/*======================================================================*/
/*                         8SVXWRITE                                    */
/*======================================================================*/

LONG st_svxwrite(ft, buf, len)
ft_t ft;
LONG *buf, len;
{
	svx_t p = (svx_t ) ft->priv;

	unsigned char datum;
	int done = 0;
	int i;

	p->nsamples += len;

	while(done < len) {
		for (i = 0; i < ft->info.channels; i++) {
			datum = RIGHT(*buf++, 24);
			/* FIXME: Needs to pass ft struct and not FILE */
			putc(datum, p->ch[i]);
		}
		done += ft->info.channels;
	}
	return (done);
}

/*======================================================================*/
/*                         8SVXSTOPWRITE                                */
/*======================================================================*/

int st_svxstopwrite(ft)
ft_t ft;
{
	svx_t p = (svx_t ) ft->priv;

	int i;
	int len;
	char svxbuf[512];

	/* append all channel pieces to channel 0 */
	/* close temp files */
	for (i = 1; i < ft->info.channels; i++) {
		if (fseek (p->ch[i], 0L, 0))
		{
			st_fail_errno (ft,errno,"Can't rewind channel output file %d",i);
			return(ST_EOF);
		}
		while (!feof(p->ch[i])) {
			len = fread (svxbuf, 1, 512, p->ch[i]);
			fwrite (svxbuf, 1, len, p->ch[0]);
		}
		fclose (p->ch[i]);
	}

	/* add a pad byte if BODY size is odd */
	if(p->nsamples % 2 != 0)
	    st_writeb(ft, '\0');

	/* fixup file sizes in header */
	if (fseek(ft->fp, 0L, 0) != 0)
	{
		st_fail_errno(ft,errno,"can't rewind output file to rewrite 8SVX header");
		return(ST_EOF);
	}
	svxwriteheader(ft, p->nsamples);
	return(ST_SUCCESS);
}

/*======================================================================*/
/*                         8SVXWRITEHEADER                              */
/*======================================================================*/
#define SVXHEADERSIZE 100
static void svxwriteheader(ft,nsamples)
ft_t ft;
LONG nsamples;
{
	LONG formsize =  nsamples + SVXHEADERSIZE - 8;

	/* FORM size must be even */
	if(formsize % 2 != 0) formsize++;

	st_writes(ft, "FORM");
	st_writedw(ft, formsize);  /* size of file */
	st_writes(ft, "8SVX"); /* File type */

	st_writes(ft, "VHDR");
	st_writedw(ft, (LONG) 20); /* number of bytes to follow */
	st_writedw(ft, nsamples);  /* samples, 1-shot */
	st_writedw(ft, (LONG) 0);  /* samples, repeat */
	st_writedw(ft, (LONG) 0);  /* samples per repeat cycle */
	st_writew(ft, (int) ft->info.rate); /* samples per second */
	st_writeb(ft,1); /* number of octabes */
	st_writeb(ft,0); /* data compression (none) */
	st_writew(ft,1); st_writew(ft,0); /* volume */

	st_writes(ft, "ANNO");
	st_writedw(ft, (LONG) 32); /* length of block */
	st_writes(ft, "File created by Sound Exchange  ");

	st_writes(ft, "CHAN");
	st_writedw(ft, (LONG) 4);
	st_writedw(ft, (ft->info.channels == 2) ? (LONG) 6 :
		   (ft->info.channels == 4) ? (LONG) 15 : (LONG) 2);

	st_writes(ft, "BODY");
	st_writedw(ft, nsamples); /* samples in file */
}
