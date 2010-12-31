/*
 * psfreedom.c -- PS3 Jailbreak exploit Gadget Driver
 *
 * Copyright (C) Youness Alaoui (KaKaRoTo)
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 3, as published by the Free Software Foundation.
 *
 * This code is based in part on:
 *
 * PSGroove
 * USB MIDI Gadget Driver, Copyright (C) 2006 Thumtronics Pty Ltd.
 * Gadget Zero driver, Copyright (C) 2003-2004 David Brownell.
 * USB Audio driver, Copyright (C) 2002 by Takashi Iwai.
 * USB MIDI driver, Copyright (C) 2002-2005 Clemens Ladisch.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#else
#include <linux/usb.h>
#include <linux/usb_gadget.h>
#endif

#define DEBUG
#define VERBOSE_DEBUG

/*-------------------------------------------------------------------------*/

MODULE_AUTHOR("Youness Alaoui (KaKaRoTo)");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Implements PS3 jailbreak exploit over USB.");

#define DRIVER_VERSION "20 November 2010"
#define PSFREEDOM_VERSION "1.1-hermes"

MODULE_VERSION(PSFREEDOM_VERSION);

#define PROC_DIR_NAME                 "psfreedom"
#define PROC_STATUS_NAME              "status"
#define PROC_VERSION_NAME             "version"
#define PROC_PAYLOAD_NAME             "payload"
#define PROC_SHELLCODE_NAME           "shellcode"

static const char shortname[] = "PSFreedom";
static const char longname[] = "PS3 Jailbreak exploit";

static short int debug = 0;

#ifdef NO_DELAYED_PORT_SWITCHING
static short int no_delayed_switching = 1;
#else
static short int no_delayed_switching = 0;
#endif

/* big enough to hold our biggest descriptor */
#define USB_BUFSIZ 4096

/* States for the state machine */
enum PsfreedomState {
  INIT,
  HUB_READY,
  DEVICE1_WAIT_READY,
  DEVICE1_READY,
  DEVICE1_WAIT_DISCONNECT,
  DEVICE1_DISCONNECTED,
  DEVICE2_WAIT_READY,
  DEVICE2_READY,
  DEVICE2_WAIT_DISCONNECT,
  DEVICE2_DISCONNECTED,
  DEVICE3_WAIT_READY,
  DEVICE3_READY,
  DEVICE3_WAIT_DISCONNECT,
  DEVICE3_DISCONNECTED,
  DEVICE4_WAIT_READY,
  DEVICE4_READY,
  DEVICE4_WAIT_DISCONNECT,
  DEVICE4_DISCONNECTED,
  DEVICE5_WAIT_READY,
  DEVICE5_CHALLENGED,
  DEVICE5_READY,
  DEVICE5_WAIT_DISCONNECT,
  DEVICE5_DISCONNECTED,
  DEVICE6_WAIT_READY,
  DEVICE6_READY,
  DONE,
};

#define STATUS_STR(s) (                                         \
      s==INIT?"INIT":                                           \
      s==HUB_READY?"HUB_READY":                                 \
      s==DEVICE1_WAIT_READY?"DEVICE1_WAIT_READY":               \
      s==DEVICE1_READY?"DEVICE1_READY":                         \
      s==DEVICE1_WAIT_DISCONNECT?"DEVICE1_WAIT_DISCONNECT":     \
      s==DEVICE1_DISCONNECTED?"DEVICE1_DISCONNECTED":           \
      s==DEVICE2_WAIT_READY?"DEVICE2_WAIT_READY":               \
      s==DEVICE2_READY?"DEVICE2_READY":                         \
      s==DEVICE2_WAIT_DISCONNECT?"DEVICE2_WAIT_DISCONNECT":     \
      s==DEVICE2_DISCONNECTED?"DEVICE2_DISCONNECTED":           \
      s==DEVICE3_WAIT_READY?"DEVICE3_WAIT_READY":               \
      s==DEVICE3_READY?"DEVICE3_READY":                         \
      s==DEVICE3_WAIT_DISCONNECT?"DEVICE3_WAIT_DISCONNECT":     \
      s==DEVICE3_DISCONNECTED?"DEVICE3_DISCONNECTED":           \
      s==DEVICE4_WAIT_READY?"DEVICE4_WAIT_READY":               \
      s==DEVICE4_READY?"DEVICE4_READY":                         \
      s==DEVICE4_WAIT_DISCONNECT?"DEVICE4_WAIT_DISCONNECT":     \
      s==DEVICE4_DISCONNECTED?"DEVICE4_DISCONNECTED":           \
      s==DEVICE5_WAIT_READY?"DEVICE5_WAIT_READY":               \
      s==DEVICE5_CHALLENGED?"DEVICE5_CHALLENGED":               \
      s==DEVICE5_READY?"DEVICE5_READY":                         \
      s==DEVICE5_WAIT_DISCONNECT?"DEVICE5_WAIT_DISCONNECT":     \
      s==DEVICE5_DISCONNECTED?"DEVICE5_DISCONNECTED":           \
      s==DEVICE6_WAIT_READY?"DEVICE6_WAIT_READY":               \
      s==DEVICE6_READY?"DEVICE6_READY":                         \
      s==DONE?"DONE":                                           \
      "UNKNOWN_STATE")

/* User-friendly string for the request */
#define REQUEST_STR(r) (                        \
      r==0x8006?"GET_DESCRIPTOR":               \
      r==0xa006?"GET_HUB_DESCRIPTOR":           \
      r==0x0009?"SET_CONFIGURATION":            \
      r==0x2303?"SET_PORT_FEATURE":             \
      r==0xa300?"GET_PORT_STATUS":              \
      r==0x2301?"CLEAR_PORT_FEATURE":           \
      r==0x010B?"SET_INTERFACE":                \
      r==0x21AA?"FREEDOM":                      \
      "UNKNOWN")

#include "hub.h"
#include "psfreedom_machine.c"

/* Out device structure */
struct psfreedom_device {
  spinlock_t            lock;
  struct usb_gadget     *gadget;
  /* for control responses */
  struct usb_request    *req;
  /* for hub interrupts */
  struct usb_request    *hub_req;
  /* The hub uses a non standard ep2in */
  struct usb_ep         *hub_ep;
  /* BULK IN for the JIG */
  struct usb_ep         *in_ep;
  /* BULK OUT for the JIG */
  struct usb_ep         *out_ep;
  /* status of the state machine */
  enum PsfreedomState   status;
  /* The port to switch to after a delay */
  int                   switch_to_port_delayed;
  /* Received length of the JIG challenge */
  int                   challenge_len;
  /* Sent length of the JIG response */
  int                   response_len;
  /* Hub port status/change */
  struct hub_port       hub_ports[6];
  /* Currently enabled port on the hub (0 == hub) */
  unsigned int          current_port;
  /* The address of all ports (0 == hub) */
  u8                    port_address[7];
  /* The port1 configuration descriptor. dynamically loaded from procfs */
  u8 *port1_config_desc;
  unsigned int port1_config_desc_size;
  /* /proc FS data */
  struct proc_dir_entry *proc_dir;
  struct proc_dir_entry *proc_status_entry;
  struct proc_dir_entry *proc_version_entry;
  struct proc_dir_entry *proc_payload_entry;
  struct proc_dir_entry *proc_shellcode_entry;
};


/* Undef these if it gets defined by the controller's include in
   psfreedom_machine.c */
#ifdef DBG
#  undef DBG
#endif
#ifdef VDBG
#  undef VDBG
#endif
#ifdef INFO
#  undef INFO
#endif
#ifdef ERROR
#  undef ERROR
#endif


#define INFO(d, fmt, args...)                   \
  dev_info(&(d)->gadget->dev , fmt , ## args)
#define ERROR(d, fmt, args...)                  \
  dev_err(&(d)->gadget->dev , fmt , ## args)

#define DBG(d, fmt, args...)                        \
  {                                                 \
    if(debug>0)                                     \
      dev_dbg(&(d)->gadget->dev , fmt , ## args);   \
  }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
#define VDBG(d, fmt, args...)                       \
  {                                                 \
    if(debug>1)                                     \
      dev_vdbg(&(d)->gadget->dev , fmt , ## args);  \
  }
#else
#define VDBG DBG
#endif


static struct usb_request *alloc_ep_req(struct usb_ep *ep, unsigned length);
static void free_ep_req(struct usb_ep *ep, struct usb_request *req);
static void __exit psfreedom_cleanup(void);

/* Timer functions and macro to run the state machine */
static int timer_added = 0;
static struct timer_list psfreedom_state_machine_timer;
#define SET_TIMER(ms) DBG (dev, "Setting timer to %dms\n", ms); \
  mod_timer (&psfreedom_state_machine_timer, jiffies + msecs_to_jiffies(ms))

#include "hub.c"
#include "psfreedom_devices.c"

static void psfreedom_state_machine_timeout(unsigned long data)
{
  struct usb_gadget *gadget = (struct usb_gadget *)data;
  struct psfreedom_device *dev = get_gadget_data (gadget);
  unsigned long flags;

  spin_lock_irqsave (&dev->lock, flags);
  DBG (dev, "Timer fired, status is %s\n", STATUS_STR (dev->status));

  /* We need to delay switching the address because otherwise we will respond
     to the request (that triggered the port switch) with address 0. So we need
     to reply with the hub's address, THEN switch to 0.
  */
  if (dev->switch_to_port_delayed >= 0)
    switch_to_port (dev, dev->switch_to_port_delayed);
  dev->switch_to_port_delayed = -1;

  switch (dev->status) {
    case HUB_READY:
        dev->status = DEVICE1_WAIT_READY;
        hub_connect_port (dev, 1);
      break;
    case DEVICE1_READY:
      dev->status = DEVICE2_WAIT_READY;
      hub_connect_port (dev, 2);
      break;
    case DEVICE2_READY:
      dev->status = DEVICE3_WAIT_READY;
      hub_connect_port (dev, 3);
      break;
    case DEVICE3_READY:
      dev->status = DEVICE2_WAIT_DISCONNECT;
      hub_disconnect_port (dev, 2);
      break;
    case DEVICE2_DISCONNECTED:
      dev->status = DEVICE4_WAIT_READY;
      hub_connect_port (dev, 4);
      break;
    case DEVICE4_READY:
      dev->status = DEVICE5_WAIT_READY;
      hub_connect_port (dev, 5);
      break;
    case DEVICE5_CHALLENGED:
      jig_response_send (dev, NULL);
      break;
    case DEVICE5_READY:
      if (no_delayed_switching) {
        /* if we can't delay the port switching, then we at this point, we can't
          disconnect the device 3... so we just unregister the driver so that
          all the devices get virtually disconnected and the exploit works.
          Since we won't exist after that, let's unlock the spinlock and return.
         */
        INFO (dev, "JAILBROKEN!!! DONE!!!!!!!!!\n");
        INFO (dev, "Congratulations, it should work now, "
              "all you need to do is pray!");
        del_timer (&psfreedom_state_machine_timer);
        timer_added = 0;
        dev->status = DONE;
        spin_unlock_irqrestore (&dev->lock, flags);
        psfreedom_cleanup ();
        return;
      } else {
        dev->status = DEVICE3_WAIT_DISCONNECT;
        hub_disconnect_port (dev, 3);
      }
      break;
    case DEVICE3_DISCONNECTED:
      dev->status = DEVICE5_WAIT_DISCONNECT;
      hub_disconnect_port (dev, 5);
      break;
    case DEVICE5_DISCONNECTED:
      dev->status = DEVICE4_WAIT_DISCONNECT;
      hub_disconnect_port (dev, 4);
      break;
    case DEVICE4_DISCONNECTED:
      dev->status = DEVICE1_WAIT_DISCONNECT;
      hub_disconnect_port (dev, 1);
      break;
    case DEVICE1_DISCONNECTED:
      dev->status = DEVICE6_WAIT_READY;
      hub_connect_port (dev, 6);
      break;
    case DEVICE6_READY:
      dev->status = DONE;
      INFO (dev, "Congratulations, worked!");
      del_timer (&psfreedom_state_machine_timer);
      timer_added = 0;
      break;
    default:
      break;
  }
  spin_unlock_irqrestore (&dev->lock, flags);

}

static struct usb_request *alloc_ep_req(struct usb_ep *ep, unsigned length)
{
  struct usb_request *req;

  req = usb_ep_alloc_request(ep, GFP_ATOMIC);
  if (req) {
    req->length = length;
    req->buf = kmalloc(length, GFP_ATOMIC);
    if (!req->buf) {
      usb_ep_free_request(ep, req);
      req = NULL;
    }
  }
  return req;
}

static void free_ep_req(struct usb_ep *ep, struct usb_request *req)
{
  kfree(req->buf);
  usb_ep_free_request(ep, req);
}

static void psfreedom_disconnect (struct usb_gadget *gadget)
{
  struct psfreedom_device *dev = get_gadget_data (gadget);
  unsigned long flags;
  int i;

  spin_lock_irqsave (&dev->lock, flags);
  INFO (dev, "Got disconnected\n");

  /* Reinitialize all device variables*/
  dev->challenge_len = 0;
  dev->response_len = 0;
  dev->current_port = 0;
  for (i = 0; i < 6; i++)
    dev->hub_ports[i].status = dev->hub_ports[i].change = 0;
  for (i = 0; i < 7; i++)
    dev->port_address[i] = 0;
  hub_disconnect (gadget);
  devices_disconnect (gadget);
  if (timer_added)
    del_timer (&psfreedom_state_machine_timer);
  timer_added = 0;
  dev->switch_to_port_delayed = -1;
  dev->status = INIT;

  spin_unlock_irqrestore (&dev->lock, flags);
}

static void psfreedom_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
  struct psfreedom_device *dev = ep->driver_data;
  unsigned long flags;

  spin_lock_irqsave (&dev->lock, flags);
  if (req->status || req->actual != req->length) {
    struct psfreedom_device * dev = (struct psfreedom_device *) ep->driver_data;
    DBG(dev, "%s setup complete FAIL --> %d, %d/%d\n",
        STATUS_STR (dev->status), req->status, req->actual, req->length);
  } else {
    VDBG(dev, "%s setup complete SUCCESS --> %d, %d/%d\n",
        STATUS_STR (dev->status), req->status, req->actual, req->length);
  }
  spin_unlock_irqrestore (&dev->lock, flags);
}

/*
 * The setup() callback implements all the ep0 functionality that's
 * not handled lower down, in hardware or the hardware driver (like
 * device and endpoint feature flags, and their status).  It's all
 * housekeeping for the gadget function we're implementing.  Most of
 * the work is in config-specific setup.
 */
static int psfreedom_setup(struct usb_gadget *gadget,
    const struct usb_ctrlrequest *ctrl)
{
  struct psfreedom_device *dev = get_gadget_data(gadget);
  struct usb_request *req = dev->req;
  int value = -EOPNOTSUPP;
  u16 w_index = le16_to_cpu(ctrl->wIndex);
  u16 w_value = le16_to_cpu(ctrl->wValue);
  u16 w_length = le16_to_cpu(ctrl->wLength);
  u8 address = psfreedom_get_address (dev->gadget);
  unsigned long flags;
  u16 request = (ctrl->bRequestType << 8) | ctrl->bRequest;

  spin_lock_irqsave (&dev->lock, flags);
  VDBG (dev, "Setup called %d (0x%x) -- %d -- %d. Myaddr :%d\n", ctrl->bRequest,
      ctrl->bRequestType, w_value, w_index, address);

  req->zero = 0;

  /* Enable the timer if it's not already enabled */
  if (timer_added == 0)
    add_timer (&psfreedom_state_machine_timer);
  timer_added = 1;

  /* Set the address of the port */
  if (address)
    dev->port_address[dev->current_port] = address;

  /* Setup the hub or the devices */
  if (dev->current_port == 0)
    value = hub_setup (gadget, ctrl, request, w_index, w_value, w_length);
  else
    value = devices_setup (gadget, ctrl, request, w_index, w_value, w_length);

  if (no_delayed_switching) {
    if (dev->switch_to_port_delayed >= 0)
      switch_to_port (dev, dev->switch_to_port_delayed);
    dev->switch_to_port_delayed = -1;
  }

  DBG (dev, "%s Setup called %s (%d - %d) -> %d (w_length=%d)\n",
      STATUS_STR (dev->status),  REQUEST_STR (request), w_value, w_index,
      value, w_length);

  /* respond with data transfer before status phase? */
  if (value >= 0) {
    req->length = value;
    req->zero = value < w_length;
    value = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
    if (value < 0) {
      DBG(dev, "ep_queue --> %d\n", value);
      req->status = 0;
      spin_unlock_irqrestore (&dev->lock, flags);
      psfreedom_setup_complete(gadget->ep0, req);
      return value;
    }
  }

  spin_unlock_irqrestore (&dev->lock, flags);
  /* device either stalls (value < 0) or reports success */
  return value;
}

int proc_shellcode_read(char *buffer, char **start, off_t offset, int count,
    int *eof, void *user_data)
{
  struct psfreedom_device *dev = user_data;
  unsigned long flags;

  INFO (dev, "proc_shellcode_read (/proc/%s/%s) called. count %d."
      "Offset 0x%p - 0x%p\n",
      PROC_DIR_NAME, PROC_PAYLOAD_NAME, count,
      (void *)offset, (void *)(offset + count));

  spin_lock_irqsave (&dev->lock, flags);
  if (offset < 40) {
    /* fill the buffer, return the buffer size */
    memcpy(buffer, jig_response + 24 + offset, 40 - offset);
  }
  *eof = 1;

  spin_unlock_irqrestore (&dev->lock, flags);

  return offset < 40 ? 40 - offset: 0;
}

int proc_shellcode_write(struct file *file, const char *buffer,
    unsigned long count, void *user_data)
{
  struct psfreedom_device *dev = user_data;
  unsigned long flags;

  INFO (dev, "proc_shellcode_write (/proc/%s/%s) called. count %lu\n",
      PROC_DIR_NAME, PROC_SHELLCODE_NAME, count);

  if (count != 40) {
    ERROR (dev, "Shellcode must be 40 bytes long! Received %lu bytes\n", count);
    return -EFAULT;
  }

  spin_lock_irqsave (&dev->lock, flags);

  DBG (dev, "Loading shellcode. Size 40\n");

  if (copy_from_user(jig_response + 24, buffer, count)) {
    spin_unlock_irqrestore (&dev->lock, flags);
    return -EFAULT;
  }

  spin_unlock_irqrestore (&dev->lock, flags);
  return count;
}

int proc_payload_read(char *buffer, char **start, off_t offset, int count,
    int *eof, void *user_data)
{
  struct psfreedom_device *dev = user_data;
  unsigned int len;
  unsigned long flags;

  spin_lock_irqsave (&dev->lock, flags);

  INFO (dev, "proc_payload_read (/proc/%s/%s) called. count %d."
      "Offset 0x%p - 0x%p\n",
      PROC_DIR_NAME, PROC_PAYLOAD_NAME, count,
      (void *)offset, (void *)(offset + count));

  len = dev->port1_config_desc_size - sizeof(port1_config_desc_prefix);

  if (len > offset)
    count = min ((int) (len - offset), count);
  else
    count = 0;

  DBG (dev, "Length is %d. Sending %d\n", len, count);

    /* fill the buffer, return the buffer size */
  if (count)
    memcpy(buffer, dev->port1_config_desc + offset +    \
        sizeof(port1_config_desc_prefix), count);
  else
    *eof = 1;

  *start = buffer;

  spin_unlock_irqrestore (&dev->lock, flags);

  return count;
}

int proc_payload_write(struct file *file, const char *buffer,
    unsigned long count, void *user_data)
{
  struct psfreedom_device *dev = user_data;
  u8 *new_config = NULL;
  unsigned int new_size = 0;
  unsigned int prefix_size = sizeof(port1_config_desc_prefix);
  unsigned long flags;

  INFO (dev, "proc_payload_write (/proc/%s/%s) called. count %lu\n",
      PROC_DIR_NAME, PROC_PAYLOAD_NAME, count);

  // hermes with prefix included
  if (count == 3840) {
    prefix_size = 0;
    INFO (dev, "Hermes payload with port1_config_desc_prefix detected, not adding prefix\n");
  } else {
    INFO (dev, "Hermes payload without port1_config_desc_prefix detected, adding %d-byte prefix\n", prefix_size);
  }
  
  new_size = count + prefix_size;
  
  new_config = kmalloc(new_size, GFP_KERNEL);
  
  if (prefix_size > 0) {
    memcpy(new_config, port1_config_desc_prefix, prefix_size);
  }
  
  if (copy_from_user(new_config + prefix_size, buffer, count)) {
    kfree (new_config);
    return -EFAULT;
  }

  spin_lock_irqsave (&dev->lock, flags);
  if (dev->port1_config_desc)
    kfree(dev->port1_config_desc);
  dev->port1_config_desc = new_config;
  dev->port1_config_desc_size = new_size;
  spin_unlock_irqrestore (&dev->lock, flags);

  return count;
}

int proc_version_read(char *buffer, char **start, off_t offset, int count,
    int *eof, void *user_data)
{
  struct psfreedom_device *dev = user_data;

  INFO (dev, "proc_version_read (/proc/%s/%s) called. count %d\n",
      PROC_DIR_NAME, PROC_VERSION_NAME, count);

  *eof = 1;
  /* fill the buffer, return the buffer size */
  return sprintf (buffer + offset, "%s\n", PSFREEDOM_VERSION);
}

int proc_status_read(char *buffer, char **start, off_t offset, int count,
    int *eof, void *user_data)
{
  struct psfreedom_device *dev = user_data;
  unsigned int len;
  unsigned long flags;

  spin_lock_irqsave (&dev->lock, flags);

  /* This file is meant to be read in a loop, so don't spam dmesg with an INFO
     message when it gets read */
  VDBG (dev, "proc_status_read (/proc/%s/%s) called. count %d\n",
      PROC_DIR_NAME, PROC_STATUS_NAME, count);

  *eof = 1;
  /* fill the buffer, return the buffer size */
  len = sprintf (buffer + offset, "%s\n", STATUS_STR (dev->status));

  spin_unlock_irqrestore (&dev->lock, flags);

  return len;
}


static void create_proc_fs (struct psfreedom_device *dev,
    struct proc_dir_entry **entry,  char *procfs_filename,
    read_proc_t read_proc, write_proc_t write_proc)
{
  /* create the /proc file */
  int permission = 0;
  if (read_proc)
    permission |= 0444;
  if (write_proc)
    permission |= 0222;
  *entry = create_proc_entry(procfs_filename, permission, dev->proc_dir);

  if (*entry == NULL) {
    ERROR (dev, "Error: Could not initialize /proc/%s/%s\n",
        PROC_DIR_NAME, procfs_filename);
  } else {
    (*entry)->read_proc  = read_proc;
    (*entry)->write_proc = write_proc;
    (*entry)->data       = dev;
    (*entry)->mode       = S_IFREG;
    if (read_proc)
      (*entry)->mode    |= S_IRUGO;
    if (write_proc)
      (*entry)->mode    |= S_IWUGO;
    (*entry)->uid        = 0;
    (*entry)->gid        = 0;
    (*entry)->size       = 0;

    INFO (dev, "/proc/%s/%s created\n", PROC_DIR_NAME, procfs_filename);
  }
}


static void /* __init_or_exit */ psfreedom_unbind(struct usb_gadget *gadget)
{
  struct psfreedom_device *dev = get_gadget_data(gadget);

  INFO (dev, "unbind\n");

  if (timer_added)
    del_timer (&psfreedom_state_machine_timer);
  timer_added = 0;

  /* we've already been disconnected ... no i/o is active */
  if (dev) {
    if (dev->port1_config_desc)
      kfree(dev->port1_config_desc);
    if (dev->req)
      free_ep_req(gadget->ep0, dev->req);
    if (dev->hub_req)
      free_ep_req(dev->hub_ep, dev->hub_req);
    if (dev->proc_status_entry)
      remove_proc_entry(PROC_STATUS_NAME, dev->proc_dir);
    if (dev->proc_version_entry)
      remove_proc_entry(PROC_VERSION_NAME, dev->proc_dir);
    if (dev->proc_payload_entry)
      remove_proc_entry(PROC_PAYLOAD_NAME, dev->proc_dir);
    if (dev->proc_shellcode_entry)
      remove_proc_entry(PROC_SHELLCODE_NAME, dev->proc_dir);
    if (dev->proc_dir)
      remove_proc_entry(PROC_DIR_NAME, NULL);
    kfree(dev);
    set_gadget_data(gadget, NULL);
  }
}


static int psfreedom_bind(struct usb_gadget *gadget)
{
  struct psfreedom_device *dev;
  int err = 0;

  dev = kzalloc(sizeof(*dev), GFP_KERNEL);
  if (!dev) {
    return -ENOMEM;
  }
  spin_lock_init(&dev->lock);
  usb_gadget_set_selfpowered (gadget);
  dev->gadget = gadget;
  set_gadget_data(gadget, dev);

  INFO(dev, "%s, version: " PSFREEDOM_VERSION " - " DRIVER_VERSION "\n",
      longname);

  DBG (dev, "Loading default payload and shellcode\n");
  dev->port1_config_desc_size = sizeof(default_payload) + \
      sizeof(port1_config_desc_prefix);
  dev->port1_config_desc = kmalloc(dev->port1_config_desc_size, GFP_KERNEL);
  memcpy(dev->port1_config_desc, port1_config_desc_prefix,
      sizeof(port1_config_desc_prefix));
  memcpy(dev->port1_config_desc + sizeof(port1_config_desc_prefix),
      default_payload, sizeof(default_payload));
  memcpy(jig_response + 24, default_shellcode, sizeof(default_shellcode));
  

  /* preallocate control response and buffer */
  dev->req = alloc_ep_req(gadget->ep0,
      max (sizeof (port3_config_desc),
          dev->port1_config_desc_size) + USB_BUFSIZ);
  if (!dev->req) {
    err = -ENOMEM;
    goto fail;
  }

  dev->req->complete = psfreedom_setup_complete;
  gadget->ep0->driver_data = dev;

  /* Bind the hub and devices */
  err = hub_bind (gadget, dev);
  if (err < 0)
    goto fail;

  err = devices_bind (gadget, dev);
  if (err < 0)
    goto fail;

  DBG(dev, "psfreedom_bind finished ok\n");

  setup_timer(&psfreedom_state_machine_timer, psfreedom_state_machine_timeout,
      (unsigned long) gadget);

  psfreedom_disconnect (gadget);

  /* Create the /proc filesystem */
  dev->proc_dir = proc_mkdir (PROC_DIR_NAME, NULL);
  if (dev->proc_dir) {
    printk(KERN_INFO "/proc/%s/ created\n", PROC_DIR_NAME);
    create_proc_fs (dev, &dev->proc_status_entry, PROC_STATUS_NAME,
        proc_status_read, NULL);
    create_proc_fs (dev, &dev->proc_version_entry, PROC_VERSION_NAME,
        proc_version_read, NULL);
    create_proc_fs (dev, &dev->proc_payload_entry, PROC_PAYLOAD_NAME,
        proc_payload_read, proc_payload_write);
    create_proc_fs (dev, &dev->proc_shellcode_entry, PROC_SHELLCODE_NAME,
        proc_shellcode_read, proc_shellcode_write);
    /* that's it for now..*/
  }

  return 0;

 fail:
  psfreedom_unbind(gadget);
  return err;
}


static void psfreedom_suspend(struct usb_gadget *gadget)
{
  struct psfreedom_device *dev = get_gadget_data(gadget);

  if (gadget->speed == USB_SPEED_UNKNOWN) {
    return;
  }

  INFO (dev, "suspend\n");
}

static void psfreedom_resume(struct usb_gadget *gadget)
{
  struct psfreedom_device *dev = get_gadget_data(gadget);

  INFO (dev, "resume\n");
}

static struct usb_gadget_driver psfreedom_driver = {
  .speed        = USB_SPEED_HIGH,
  .function     = (char *)longname,

  .bind         = psfreedom_bind,
  .unbind       = psfreedom_unbind,

  .setup        = psfreedom_setup,
  .disconnect   = psfreedom_disconnect,

  .suspend      = psfreedom_suspend,
  .resume       = psfreedom_resume,

  .driver       = {
    .name               = (char *)shortname,
    .owner              = THIS_MODULE,
  },
};

static int __init psfreedom_init(void)
{
  int ret = 0;

  printk(KERN_INFO "init\n");

  if (debug)
    printk(KERN_INFO "Debug mode enabled. Level is %d\n", debug);

  if (no_delayed_switching)
    printk(KERN_INFO "No delayed port switching enabled.\n");

  /* Determine what speed the controller supports */
  if (psfreedom_is_high_speed ())
    psfreedom_driver.speed = USB_SPEED_HIGH;
  else if (psfreedom_is_low_speed ())
    psfreedom_driver.speed = USB_SPEED_HIGH;
  else
    psfreedom_driver.speed = USB_SPEED_FULL;

  ret = usb_gadget_register_driver(&psfreedom_driver);

  printk(KERN_INFO "register driver returned %d\n", ret);

  return ret;
}
module_init(psfreedom_init);

static void __exit psfreedom_cleanup(void)
{
  usb_gadget_unregister_driver(&psfreedom_driver);
}
module_exit(psfreedom_cleanup);

