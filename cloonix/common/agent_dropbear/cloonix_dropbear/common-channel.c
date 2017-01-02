/****************************************************************************/
/* Copy-pasted-modified for cloonix                License GPL-3.0+         */
/*--------------------------------------------------------------------------*/
/* Original code from:                                                      */
/*                            Dropbear SSH                                  */
/*                            Matt Johnston                                 */
/****************************************************************************/
#include "includes.h"
#include "session.h"
#include "packet.h"
#include "ssh.h"
#include "buffer.h"
#include "circbuffer.h"
#include "dbutil.h"
#include "channel.h"
#include "ssh.h"
#include "listener.h"
#include "runopts.h"
#include "chansession.h"
#include "io_clownix.h"

int main_i_run_in_kvm(void);
void call_child_death_detection(void);
size_t cloonix_read(int fd, void *ibuf, size_t count);
size_t cloonix_write(int fd, const void *ibuf, size_t count);
static void send_msg_channel_open_failure(unsigned int remotechan, int reason,
		                          char *text, char *lang);
static void send_msg_channel_open_confirmation(struct Channel* channel,
		unsigned int recvwindow, 
		unsigned int recvmaxpacket);
int writechannel(struct Channel* channel, int fd, circbuffer *cbuf);
static void send_msg_channel_window_adjust(struct Channel *channel, 
		unsigned int incr);
int send_msg_channel_data(struct Channel *channel, int isextended);
static void remove_channel(struct Channel *channel);
void check_in_progress(struct Channel *channel);
void check_close(struct Channel *channel);
static void close_chan_fd(struct Channel *channel, int fd);


#define ERRFD_IS_READ(channel) ((channel)->extrabuf == NULL)
#define ERRFD_IS_WRITE(channel) (!ERRFD_IS_READ(channel))

/* allow space for:
 * 1 byte  byte      SSH_MSG_CHANNEL_DATA
 * 4 bytes uint32    recipient channel
 * 4 bytes string    data
 */
#define RECV_MAX_CHANNEL_DATA_LEN (RECV_MAX_PAYLOAD_LEN-(1+4+4))


/****************************************************************************/
void chancleanup() 
{
  remove_channel(&ses.channel);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void chan_initwritebuf(struct Channel *channel)
{
  if (channel->init_done)
    {
    if ((channel->writebuf->size != 0) || (channel->recvwindow != 0))
      KOUT("%d %d ", channel->writebuf->size, channel->recvwindow);
    cbuf_free(channel->writebuf);
    channel->writebuf = cbuf_new(opts.recv_window);
    channel->recvwindow = opts.recv_window;
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static struct Channel* getchannel_msg(const char* kind)
{
  unsigned int chan;
  chan = buf_getint(ses.payload);
  if (chan != 0)
    KOUT("%s  %d", kind, chan);
  if (ses.channel.init_done)
    return &(ses.channel);
  else
    return NULL;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
struct Channel* getchannel() {
	return getchannel_msg(NULL);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void check_fd_and_close_on_error(struct Channel *channel) 
{
  int err, val;
  if (channel->errfd != -1)
    {
    err = ioctl(channel->errfd, SIOCINQ, &val);
    if ((err != 0) || (val < 0))
      {
      close_chan_fd(channel, channel->errfd);
      }
    }
  if (channel->readfd != -1)
    {
    err = ioctl(channel->readfd, SIOCINQ, &val);
    if ((err != 0) || (val < 0))
      {
      close_chan_fd(channel, channel->readfd);
      }
    }
  if (channel->writefd != -1)
    {
    err = ioctl(channel->writefd, SIOCOUTQ, &val);
    if (err != 0)
      {
      close_chan_fd(channel, channel->writefd);
      }
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static int sock_out_is_empty(void)
{ 
  int err, val, result = 0;
  if (isempty(&(ses.writequeue)))
    {
    err = ioctl(ses.sock_out, SIOCOUTQ, &val);
    if (err != 0)
      result = 1;
    else if (val == 0)
      result = 1;
    }
  return result;
}
/*--------------------------------------------------------------------------*/


/****************************************************************************/
void check_close(struct Channel *channel) 
{
  call_child_death_detection();
  if ((channel->ctype->check_close) &&
      (channel->ctype->check_close(channel)))
    {
    if (channel->flushing == 0)
      { 
      check_fd_and_close_on_error(channel); 
      if ((channel->errfd == -1) && 
          (channel->readfd == -1) &&
          (channel->writefd == -1))
        {
        channel->flushing = 1;
        }
      }
    else if (channel->flushing == 1)
      {
      if (sock_out_is_empty())
        channel->flushing = 2;
      }
    else if (channel->flushing == 2)
      {
      wrapper_exit(0, (char *)__FILE__, __LINE__);
      }
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void check_in_progress(struct Channel *channel)
{
  int val;
  socklen_t vallen = sizeof(val);
  if ((getsockopt(channel->writefd, SOL_SOCKET, SO_ERROR, &val, &vallen))|| 
      (val != 0))
    {
    KERR(" ");
    send_msg_channel_open_failure(channel->remotechan,
    SSH_OPEN_CONNECT_FAILED, "", "");
    close(channel->writefd);
    remove_channel(channel);
    }
  else
    {
    chan_initwritebuf(channel);
    send_msg_channel_open_confirmation(channel,
                                       channel->recvwindow,
                                       channel->recvmaxpacket);
    channel->readfd = channel->writefd;
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
int writechannel(struct Channel* channel, int fd, circbuffer *cbuf) 
{
  int len, maxlen, result = 0;
  maxlen = cbuf_readlen(cbuf);
  if (maxlen > 0)
    {
    len = cloonix_write(fd, cbuf_readptr(cbuf, maxlen), maxlen);
    if (len <= 0)
      {
      if (len < 0 && ((errno != EINTR) && (errno != EAGAIN)))
        {
        result = -1;
        close_chan_fd(channel, fd);
        }
      return result;
      }
    if (len != maxlen)
      result = -2;
    cbuf_incrread(cbuf, len);
    channel->recvdonelen += len;
    if (channel->recvdonelen >= RECV_WINDOWEXTEND)
      {
      send_msg_channel_window_adjust(channel, channel->recvdonelen);
      channel->recvwindow += channel->recvdonelen;
      channel->recvdonelen = 0;
      }
    if (channel->recvwindow > opts.recv_window)
      KOUT(" ");
    if (channel->recvwindow > cbuf_getavail(channel->writebuf))
      KOUT(" ");
    if ((channel->extrabuf) &&
        (channel->recvwindow > cbuf_getavail(channel->extrabuf)))
      KOUT(" ");
    fdatasync(fd);
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void remove_channel(struct Channel * channel) 
{
  if (channel->init_done)
  {
  cbuf_free(channel->writebuf);
  channel->writebuf = NULL;
  if (channel->extrabuf)
    {
    cbuf_free(channel->extrabuf);
    channel->extrabuf = NULL;
    }
  if (IS_DROPBEAR_SERVER || (channel->writefd != STDOUT_FILENO)) 
    {
    close(channel->writefd);
    close(channel->readfd);
    close(channel->errfd);
    }
  if (!(channel->close_handler_done) &&
       (channel->ctype->closehandler))
    {
    channel->ctype->closehandler(channel);
    channel->close_handler_done = 1;
    }
  if (!isempty(&ses.writequeue))
    KERR("not empty");
  }
  memset(channel, 0, sizeof(struct Channel)); 
  wrapper_exit(0, (char *)__FILE__, __LINE__);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void recv_msg_channel_request()
{
  int wantreply;
  struct Channel *channel;
  channel = getchannel();
  if (!channel)
    KERR(" ");
  else if ((channel->ctype->reqhandler) && 
           (!channel->close_handler_done))
    {
    channel->ctype->reqhandler(channel);
    }
  else
    {
    KERR(" ");
    buf_eatstring(ses.payload);
    wantreply = buf_getbool(ses.payload);
    if (wantreply)
      send_msg_channel_failure(channel);
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
int send_msg_channel_data(struct Channel *channel, int isextended)
{
  int err, val, fd, len, result = 0;
  size_t maxlen, size_pos;
  if (isextended)
    fd = channel->errfd;
  else
    fd = channel->readfd;
  if (fd < 0)
    KOUT(" ");

  err = ioctl(fd, SIOCINQ, &val);
  if ((err != 0) || (val <= 0))
    {
    close_chan_fd(channel, fd);
    result = -1;
    }
  else
    {
    maxlen = MIN(channel->transwindow, channel->transmaxpacket);
    maxlen = MIN(maxlen,
                 ses.writepayload->size - 1 - 4 - 4 - (isextended ? 4 : 0));
    if ((val > 0) && (maxlen >= val))
      {
      buf_putbyte(ses.writepayload,
      isextended ? SSH_MSG_CHANNEL_EXTENDED_DATA : SSH_MSG_CHANNEL_DATA);
      buf_putint(ses.writepayload, channel->remotechan);
      if (isextended)
        buf_putint(ses.writepayload, SSH_EXTENDED_DATA_STDERR);
      size_pos = ses.writepayload->pos;
      buf_putint(ses.writepayload, 0);
      len = cloonix_read(fd, buf_getwriteptr(ses.writepayload, maxlen), maxlen);
      if (len <= 0)
        KOUT(" ");
      if (len < val)
        KERR("%d %d %d", (int) maxlen, len, val);
      buf_incrwritepos(ses.writepayload, len);
      buf_setpos(ses.writepayload, size_pos);
      buf_putint(ses.writepayload, len);
      channel->transwindow -= len;
      encrypt_packet();
      }
    }
  return result;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void recv_msg_channel_data()
{
  struct Channel *channel;
  channel = getchannel();
  if (!channel)
    KERR(" ");
  else
    common_recv_msg_channel_data(channel,
                                 channel->writefd,
                                 channel->writebuf);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void common_recv_msg_channel_data(struct Channel *channel, 
                                  int fd, circbuffer * cbuf)
{
  unsigned int datalen;
  unsigned int maxdata;
  unsigned int buflen;
  unsigned int len;
  if ((fd >= 0) && (cbuf))
    {
    datalen = buf_getint(ses.payload);
    maxdata = cbuf_getavail(cbuf);
    if (datalen > maxdata)
      KOUT("Oversized packet %d %d", datalen, maxdata);
    len = datalen;
    while (len > 0)
      {
      buflen = cbuf_writelen(cbuf);
      buflen = MIN(buflen, len);
      memcpy(cbuf_writeptr(cbuf, buflen), 
             buf_getptr(ses.payload, buflen), buflen);
      cbuf_incrwrite(cbuf, buflen);
      buf_incrpos(ses.payload, buflen);
      len -= buflen;
      }
    if (channel->recvwindow < datalen)
      KOUT(" ");
    channel->recvwindow -= datalen;
    if (channel->recvwindow > opts.recv_window)
      KOUT(" ");
    }
  else
    wrapper_exit(0, (char *)__FILE__, __LINE__);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void recv_msg_channel_window_adjust(void)
{
  struct Channel * channel;
  unsigned int incr;
  channel = getchannel();
  if (!channel)
    KERR(" ");
  else
    {
    incr = buf_getint(ses.payload);
    channel->transwindow += incr;
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void send_msg_channel_window_adjust(struct Channel* channel, 
                                           unsigned int incr)
{
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_WINDOW_ADJUST);
  buf_putint(ses.writepayload, channel->remotechan);
  buf_putint(ses.writepayload, incr);
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/
	
/****************************************************************************/
void recv_msg_channel_open(void) 
{
  char *type;
  unsigned int typelen, transmaxpacket;
  struct Channel *channel = &ses.channel;
  unsigned int errtype = SSH_OPEN_UNKNOWN_CHANNEL_TYPE;
  int ret;
  type = buf_getstring(ses.payload, &typelen);
  channel->remotechan =  buf_getint(ses.payload);
  channel->transwindow = buf_getint(ses.payload);
  transmaxpacket = buf_getint(ses.payload);
  transmaxpacket = MIN(transmaxpacket, TRANS_MAX_PAYLOAD_LEN);
  channel->transmaxpacket = transmaxpacket;
  channel->init_done = 1;
  channel->writefd = -2;
  channel->readfd = -2;
  channel->errfd = -1;
  channel->writebuf = cbuf_new(0);
  channel->recvmaxpacket = RECV_MAX_CHANNEL_DATA_LEN;
  channel->i_run_in_kvm = main_i_run_in_kvm();


  if (typelen > MAX_NAME_LEN) 
    {
    send_msg_channel_open_failure(channel->remotechan, errtype, "", "");
    KERR("%d %s", typelen, type);
    }
  else if (strcmp(type, ses.chantype->name)) 
    {
    send_msg_channel_open_failure(channel->remotechan, errtype, "", "");
    KERR("%d %s", typelen, type);
    }
  else
    {
    
    if (channel->ctype->inithandler) 
      {
      ret = channel->ctype->inithandler(channel);
      if (ret == SSH_OPEN_IN_PROGRESS) 
        {
	m_free(type);
        }
      else if (ret > 0) 
        {
        errtype = ret;
        KERR("%p", channel);
        remove_channel(channel);
        send_msg_channel_open_failure(channel->remotechan, errtype, "", "");
        KERR("%d %s", typelen, type);
	}
      else
        {
	chan_initwritebuf(channel);
	send_msg_channel_open_confirmation(channel, channel->recvwindow,
			channel->recvmaxpacket);
	m_free(type);
        }
      }
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void send_msg_channel_failure(struct Channel *channel)
{
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_FAILURE);
  buf_putint(ses.writepayload, channel->remotechan);
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void send_msg_channel_success(struct Channel *channel)
{
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_SUCCESS);
  buf_putint(ses.writepayload, channel->remotechan);
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void send_msg_channel_open_failure(unsigned int remotechan, 
                                          int reason, char *text, 
                                          char *lang)
{
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_OPEN_FAILURE);
  buf_putint(ses.writepayload, remotechan);
  buf_putint(ses.writepayload, reason);
  buf_putstring(ses.writepayload, text, strlen((char*)text));
  buf_putstring(ses.writepayload, lang, strlen((char*)lang));
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void send_msg_channel_open_confirmation(struct Channel* channel,
                                               unsigned int recvwindow, 
                                               unsigned int recvmaxpacket)
{
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_OPEN_CONFIRMATION);
  buf_putint(ses.writepayload, channel->remotechan);
  buf_putint(ses.writepayload, 0);
  buf_putint(ses.writepayload, recvwindow);
  buf_putint(ses.writepayload, recvmaxpacket);
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
static void close_chan_fd(struct Channel *channel, int fd)
{
  close(fd);
  if (fd == channel->readfd)
    channel->readfd = -1;
  if (fd == channel->errfd)
    channel->errfd = -1;
  if (fd == channel->writefd)
    channel->writefd = -1;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
int send_msg_channel_open_init(int fd) 
{
  struct Channel *chan = &ses.channel;
  chan->init_done = 1;
  chan->writefd = fd;
  chan->readfd = fd;
  chan->errfd = -1; 
  chan->recvmaxpacket = RECV_MAX_CHANNEL_DATA_LEN;
  chan->i_run_in_kvm = main_i_run_in_kvm();
  chan->writebuf = cbuf_new(opts.recv_window);
  chan->recvwindow = opts.recv_window;
  setnonblocking(fd);
  ses.maxfd = MAX(ses.maxfd, fd);
  chan->await_open = 1;
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_OPEN);
  buf_putstring(ses.writepayload, "session", strlen("session"));
  buf_putint(ses.writepayload, 0);
  buf_putint(ses.writepayload, opts.recv_window);
  buf_putint(ses.writepayload, RECV_MAX_CHANNEL_DATA_LEN);
  return DROPBEAR_SUCCESS;
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void recv_msg_channel_open_confirmation()
{
  struct Channel * channel;
  channel = getchannel();
  if (!channel)
    KERR(" ");
  else
    {
    if (!channel->await_open)
      KOUT("Unexpected channel reply");
    channel->await_open = 0;
    channel->remotechan =  buf_getint(ses.payload);
    channel->transwindow = buf_getint(ses.payload);
    channel->transmaxpacket = buf_getint(ses.payload);
    if (channel->ctype->inithandler)
      channel->ctype->inithandler(channel);
    }
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void recv_msg_channel_open_failure() 
{
  struct Channel * channel;
  channel = getchannel();
  KERR("%p", channel);
  if (!channel)
    return;
  if (!channel->await_open) 
    KOUT("Unexpected channel reply");
  channel->await_open = 0;
  remove_channel(channel);
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void send_msg_request_success() 
{
  buf_putbyte(ses.writepayload, SSH_MSG_REQUEST_SUCCESS);
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void send_msg_request_failure() 
{
  buf_putbyte(ses.writepayload, SSH_MSG_REQUEST_FAILURE);
  encrypt_packet();
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void start_send_channel_request(struct Channel *channel, char *type) 
{
  buf_putbyte(ses.writepayload, SSH_MSG_CHANNEL_REQUEST);
  buf_putint(ses.writepayload, channel->remotechan);
  buf_putstring(ses.writepayload, type, strlen(type));
}
/*--------------------------------------------------------------------------*/

/****************************************************************************/
void wrapper_exit(int val, char *file, int line)
{
  (void) file;
  (void) line;
  exit(val);
}
/*--------------------------------------------------------------------------*/
