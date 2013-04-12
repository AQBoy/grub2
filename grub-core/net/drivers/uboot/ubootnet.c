/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/net/netbuff.h>
#include <grub/uboot/disk.h>
#include <grub/uboot/uboot.h>
#include <grub/dl.h>
#include <grub/net.h>
#include <grub/time.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct ubootnet_data
{
  void *cookie;
  int handle;
};

static grub_err_t
card_open (struct grub_net_card *dev)
{
  int status;
  struct ubootnet_data *data = dev->data;

  status = uboot_dev_open (data->handle);
  if (status)
    return grub_error (GRUB_ERR_IO, "Couldn't open network card.");

  return GRUB_ERR_NONE;
}

static void
card_close (struct grub_net_card *dev)
{
  struct ubootnet_data *data = dev->data;

  uboot_dev_close (data->handle);
}

static grub_err_t
send_card_buffer (struct grub_net_card *dev, struct grub_net_buff *pack)
{
  int status;
  struct ubootnet_data *data = dev->data;
  grub_size_t len;

  len = (pack->tail - pack->data);
  if (len > dev->mtu)
    len = dev->mtu;

  grub_memcpy (dev->txbuf, pack->data, len);
  status = uboot_dev_send (data->handle, dev->txbuf,
			    len);

  if (status)
    return grub_error (GRUB_ERR_IO, N_("couldn't send network packet"));
  return GRUB_ERR_NONE;
}

static struct grub_net_buff *
get_card_packet (struct grub_net_card *dev)
{
  int rc;
  struct ubootnet_data *data = dev->data;
  grub_uint64_t start_time;
  struct grub_net_buff *nb;
  int actual;

  nb = grub_netbuff_alloc (dev->mtu + 64 + 2);
  if (!nb)
    {
      grub_netbuff_free (nb);
      return NULL;
    }
  /* Reserve 2 bytes so that 2 + 14/18 bytes of ethernet header is divisible
     by 4. So that IP header is aligned on 4 bytes. */
  grub_netbuff_reserve (nb, 2);

  start_time = grub_get_time_ms ();
  do
    {
      rc = uboot_dev_recv (data->handle, nb->data, dev->mtu + 64, &actual);
      grub_dprintf ("net", "rc=%d, actual=%d, time=%lld\n", rc, actual,
		    grub_get_time_ms () - start_time);
    }
  while ((actual <= 0 || rc < 0) && (grub_get_time_ms () - start_time < 200));
  if (actual > 0)
    {
      grub_netbuff_put (nb, actual);
      return nb;
    }
  grub_netbuff_free (nb);
  return NULL;
}

static struct grub_net_card_driver ubootnet =
  {
    .name = "ubnet",
    .open = card_open,
    .close = card_close,
    .send = send_card_buffer,
    .recv = get_card_packet
  };

GRUB_MOD_INIT (ubootnet)
{
  int devcount, i;
  int nfound = 0;

  devcount = uboot_dev_enum ();

  for (i = 0; i < devcount; i++)
    {
      struct device_info *devinfo = uboot_dev_get (i);
      struct ubootnet_data *ubdata;
      struct grub_net_card *card;

      if (!(devinfo->type & DEV_TYP_NET))
	continue;
      
      ubdata = grub_malloc (sizeof (struct ubootnet_data));
      if (!ubdata)
	{
	  grub_print_error ();
	  return;
	}
      card = grub_zalloc (sizeof (struct grub_net_card));
      if (!card)
	{
	  grub_free (ubdata);
	  grub_print_error ();
	  return;
	}

      ubdata->handle = i;
      ubdata->cookie = devinfo->cookie;

      /* FIXME: Any way to check this?  */
      card->mtu = 1500;

      grub_memcpy (&(card->default_address.mac), &devinfo->di_net.hwaddr, 6);
      card->default_address.type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;

      card->txbufsize = ALIGN_UP (card->mtu, 64) + 256;
      card->txbuf = grub_zalloc (card->txbufsize);
      if (!card->txbuf)
	{
	  grub_print_error ();
	  continue;
	}

      card->data = ubdata;
      card->flags = 0;
      card->name = grub_xasprintf ("ubnet_%d", ++nfound);
      card->idle_poll_delay_ms = 10;

      card->driver = &ubootnet;
      grub_net_card_register (card);
    }
}

GRUB_MOD_FINI (ubootnet)
{
  struct grub_net_card *card, *next;

  FOR_NET_CARDS_SAFE (card, next) 
    if (card->driver && grub_strcmp (card->driver->name, "ubnet") == 0)
      grub_net_card_unregister (card);
}
