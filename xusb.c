/* Why's it xpad4? Where's xpad1, 2, 3, 4? 
 * xpad2 was garbage. I scrapped it out of bad design. It was my first kernel module. 
 * xpad3 is still on my repo. It's incomplete. I stopped developing it because the design is near impossible to correctly implement.
 * xpad4 is the successor to them all! Well, it tries to be anyways...
 * I changed names also because I don't want to confuse myself when referring to things in notes. 
 * 
 * See, the Xbox controllers are actually HID devices! They're just stripped of their descriptors completely because Microsoft is an asshole at times.
 * There's a few problems: 
 * 1) The controllers do not advertise themselves as HID compliant devices. Because they aren't. 
 * 2) Side effect of 1, Linux HID subsystem won't see the controller at all and we can't force it to see it in a reliable manner. 
 * 
 * The way to fix this is to replace usbhid with a driver that will see the device. We can also use that oppurtunity to customize the driver
 * to be more optimized for Microsoft console controllers.
 * 
 * xusb for microsoft is essentially the low-level USB driver that handles the controllers from the driver level. Xinput is the interface. 
 * I plan on implementing xusb via this driver and xinput via ioctls. This will not remove the ability to use the Linux input subsystem with the controllers...
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wired Controller Driver v4");
MODULE_LICENSE("GPL");

/* Maximum packet size is the same for all controllers. */
#define XUSB_PACKET_SIZE 32

struct xusb_urb {
	struct urb *urb;
	char *buffer;
	dma_addr_t dma;
};

struct xusb_device {
	struct hid_device *hid;
	struct usb_interface *intf;
	
	struct xusb_urb in;
	struct xusb_urb out;
};

/* The following descriptor is from Xfree360. Shoutout to Tatoobogle as well.  */
char x360_report_descriptor[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game Pad)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x05, 0x01,                    //   USAGE_PAGE (Generic Desktop)
    0x09, 0x3a,                    //   USAGE (Counted Buffer)
    0xa1, 0x02,                    //   COLLECTION (Logical)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x3f,                    //     USAGE (Reserved)
    0x09, 0x3b,                    //     USAGE (Byte Count)
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x0c,                    //     USAGE_MINIMUM (Button 12)
    0x29, 0x0f,                    //     USAGE_MAXIMUM (Button 15)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x09, 0x09,                    //     USAGE (Button 9)
    0x09, 0x0a,                    //     USAGE (Button 10)
    0x09, 0x07,                    //     USAGE (Button 7)
    0x09, 0x08,                    //     USAGE (Button 8)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x09, 0x05,                    //     USAGE (Button 5)
    0x09, 0x06,                    //     USAGE (Button 6)
    0x09, 0x0b,                    //     USAGE (Button 11)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x45, 0x01,                    //     PHYSICAL_MAXIMUM (1)
    0x95, 0x04,                    //     REPORT_COUNT (4)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x04,                    //     USAGE_MAXIMUM (Button 4)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //     LOGICAL_MAXIMUM (255)
    0x35, 0x00,                    //     PHYSICAL_MINIMUM (0)
    0x46, 0xff, 0x00,              //     PHYSICAL_MAXIMUM (255)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x32,                    //     USAGE (Z)
    0x09, 0x35,                    //     USAGE (Rz)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x75, 0x10,                    //     REPORT_SIZE (16)
    0x16, 0x00, 0x80,              //     LOGICAL_MINIMUM (-32768)
    0x26, 0xff, 0x7f,              //     LOGICAL_MAXIMUM (32767)
    0x36, 0x00, 0x80,              //     PHYSICAL_MINIMUM (-32768)
    0x46, 0xff, 0x7f,              //     PHYSICAL_MAXIMUM (32767)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    //     USAGE (Pointer)
    0xa1, 0x00,                    //     COLLECTION (Physical)
    0x95, 0x02,                    //       REPORT_COUNT (2)
    0x05, 0x01,                    //       USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //       USAGE (X)
    0x09, 0x31,                    //       USAGE (Y)
    0x81, 0x02,                    //       INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    //     USAGE (Pointer)
    0xa1, 0x00,                    //     COLLECTION (Physical)
    0x95, 0x02,                    //       REPORT_COUNT (2)
    0x05, 0x01,                    //       USAGE_PAGE (Generic Desktop)
    0x09, 0x33,                    //       USAGE (Rx)
    0x09, 0x34,                    //       USAGE (Ry)
    0x81, 0x02,                    //       INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};

static struct usb_device_id xusb_id_table[] = {
	/* For xbox360 wired controllers, the interface with protocol 1 handles button-based input, LEDs, and rumbling. */
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x028e, 1) }, /* Official Wired Xbox360 controller */
	{}
};

int xusb_alloc_urb(struct xusb_urb* urb, struct usb_interface *intf, gfp_t mem_flag)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	
	urb->urb = usb_alloc_urb(0, mem_flag);
	if (!urb->urb) 
		return -ENOMEM;
	
	urb->buffer = usb_alloc_coherent(usb_dev, XUSB_PACKET_SIZE, mem_flag, &urb->dma);
	if (!urb->buffer)
		goto fail_coherent;
	
	return 0;
	
fail_coherent:
	usb_free_urb(urb->urb);
	return -ENOMEM;
}

void xusb_free_urb(struct xusb_urb* urb, struct usb_interface *intf)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	
	usb_kill_urb(urb->urb);
	usb_free_urb(urb->urb);
	usb_free_coherent(usb_dev, XUSB_PACKET_SIZE, urb->buffer, urb->dma);
}

void xusb_setup_urb(struct xusb_urb* urb, struct usb_interface *intf, 
		    usb_complete_t irq, int direction)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep = &intf->cur_altsetting->endpoint[direction ? 0 : 1].desc;
	int pipe = ((PIPE_INTERRUPT << 30) | __create_pipe(usb_dev, ep->bEndpointAddress) | direction); /* *sigh* */
	
	usb_fill_int_urb(
		urb->urb, usb_dev,
		pipe, urb->buffer, XUSB_PACKET_SIZE,
		irq, usb_get_intfdata(intf), ep->bInterval);
	
	urb->urb->transfer_dma = urb->dma;
	urb->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
}

void xusb_irq_in(struct urb *urb)
{
	struct xusb_device *controller = urb->context;
	
	switch(urb->status) {
	case 0: /* success */
		hid_input_report(controller->hid, HID_INPUT_REPORT,
			         urb->transfer_buffer,
			         urb->actual_length, 1);
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		hid_warn(urb->dev, "input irq status %d received\n", urb->status);
	}
	
	usb_submit_urb(urb, GFP_ATOMIC);
	return;
}

void xusb_irq_out(struct urb *urb)
{
	/* Nothing for now. */
}

int xusb_hid_parse(struct hid_device *hdev)
{
	hid_parse_report(hdev, x360_report_descriptor, sizeof(x360_report_descriptor));
	return 0;
}

int xusb_hid_start(struct hid_device *hdev)
{
	int error;
	struct xusb_device *controller = hdev->driver_data;

	
	/* All Xbox controllers use one interface for input/output a specific section.
	 * For instance, all buttons are handled via the interface with protocol 1 for 360 wired controllers.
	 * Each interface such as that described above only have two endpoints: 0x01 (in) and 0x81 (out). 
	 * Because of this fact, we can just allocate two urbs for that specific interface instead of going over every endpoint.*/
	error = xusb_alloc_urb(&controller->in, controller->intf, GFP_KERNEL);
	if (error)
		return error;
	
	xusb_setup_urb(&controller->in, controller->intf, xusb_irq_in, USB_DIR_IN);
	
	error = xusb_alloc_urb(&controller->out, controller->intf, GFP_KERNEL);
	if (error)
		goto fail_alloc_out;
	
	xusb_setup_urb(&controller->out, controller->intf, xusb_irq_out, USB_DIR_OUT);
	
	/* We have no need for the ctrl endpoint that I can see. */
	/* TODO: Need to send LED report to controller to let user know it's connected. */
	/* TODO: Wireless will have to submit urb here since we receive status updates with it. Separate drivers? */
	
	return 0;
	
fail_alloc_out:
	xusb_free_urb(&controller->in, controller->intf);
	return error;
}

void xusb_hid_stop(struct hid_device *hdev)
{

	struct xusb_device *controller = hdev->driver_data;
	
	xusb_free_urb(&controller->in, controller->intf);
	xusb_free_urb(&controller->out, controller->intf);
}

int xusb_hid_open(struct hid_device *hdev)
{
#if 0
	struct xusb_device *controller = hdev->driver_data;
	
	if (!hdev->open) {
		++hdev->open;
		return usb_submit_urb(controller->in.urb, GFP_KERNEL);
	}
#endif
	return 0;
}

void xusb_hid_close(struct hid_device *hdev)
{
#if 0
	struct xusb_device *controller = hdev->driver_data;
	--hdev->open;
	
	if (!hdev->open) {
		usb_kill_urb(controller->in.urb);
	}
#endif
}

int xusb_hid_raw_request(
	struct hid_device* hdev, unsigned char reportnum,
	__u8 *buf, size_t len, unsigned char rtype, int reqtype)
{
	return 0;
}

static struct hid_ll_driver xusb_hid = {
	.parse = xusb_hid_parse,
	.start = xusb_hid_start,
	.stop = xusb_hid_stop,
	.open = xusb_hid_open,
	.close = xusb_hid_close,
	.raw_request = xusb_hid_raw_request
};

static int xusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int error;
	struct hid_device *hid;
	struct xusb_device *controller = kzalloc(sizeof(struct xusb_device), GFP_KERNEL);
	struct usb_device *dev = interface_to_usbdev(intf);
	
	if (!controller) return -ENOMEM;
	
	hid = hid_allocate_device();
	if (!hid) {
		error = -ENOMEM;
		goto fail_hid_allocate;
	}
	
	hid->ll_driver = &xusb_hid;
	hid->dev.parent = &intf->dev;
	hid->bus = BUS_USB;
	hid->vendor = le16_to_cpu(dev->descriptor.idVendor);
	hid->product = le16_to_cpu(dev->descriptor.idProduct);
	hid->driver_data = controller;
	
	strlcpy(hid->name, "Xbox 360 Wired Controller", sizeof(hid->name));
	
	usb_make_path(dev, hid->phys, sizeof(hid->phys));
	strlcat(hid->phys, "/input0", sizeof(hid->phys)); /* FIXME: Need to change 0 to a variable*/
	
	controller->hid = hid;
	controller->intf = intf;
	
	error = hid_add_device(hid);
	if (error) goto fail_hid_add_device;
	
	usb_set_intfdata(intf, controller);
	
	return 0;
	
fail_hid_add_device:
	hid_destroy_device(hid);
fail_hid_allocate:
	kfree(controller);
	
	return error;
}

static void xusb_disconnect(struct usb_interface *intf)
{
	struct xusb_device* controller = usb_get_intfdata(intf);
	
	hid_destroy_device(controller->hid);
	kfree(controller);
}

static struct usb_driver xusb_driver = {
	.name = "x360",
	.probe = xusb_probe,
	.disconnect = xusb_disconnect,
	.id_table = xusb_id_table
};

MODULE_DEVICE_TABLE(usb, xusb_id_table);
module_usb_driver(xusb_driver);