/*
 * Display block devtype connected usb devices
 * with 'partition' attribute
 * based on: http://www.signal11.us/oss/udev/
 */
/* c & unix */
#include <libudev.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>

/* c++ */
#include <list>
#include <string>
#include <iostream>

using namespace std;

bool is_parent_usb(struct udev_device *dev)
{
    dev = udev_device_get_parent_with_subsystem_devtype(dev,"usb", "usb_device");
    if (dev)
        return true;

    return false;
}

std::list<std::string> enumerate()
{
    struct udev *udev = udev_new();
    if (!udev) {
        cout << "Can't create udev." << endl;
        exit(1);
    }

    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev;
    std::list<std::string> rdevices;

    /* Create a list of the devices in the 'block' subsystem. */
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "block"); //devtype == 'partition', but since this
                                                            //function does not have parameter 'devtype' we'll manually filter 
                                                            //by looking through attributes with udev_device_get_sysattr_value()
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);

        const char* value = udev_device_get_sysattr_value(dev, "partition");
        if (value) {
            const char* devpath = udev_device_get_devnode(dev);

            if(dev && is_parent_usb(dev)) {
                rdevices.emplace_back(devpath);
                udev_device_unref(dev);
            }
        }
    }
    /* Free the enumerator object */
    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return rdevices;
}

void monitor()
{
    struct udev *udev = udev_new();
    if (!udev) {
        cout << "Can't create udev." << endl;
        exit(1);
    }

    /* Set up a monitor to monitor hidraw devices */
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "block", "partition");
    udev_monitor_enable_receiving(mon);
    
    int fd = udev_monitor_get_fd(mon);
    struct udev_device *dev;

    //non-blocking udev_monitor
    while (1) {
        fd_set fds;
        struct timeval tv;
        int ret;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        ret = select(fd+1, &fds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(fd, &fds)) {
            printf("\nselect() says there should be data\n");

            //will not block since select says our fd has data
            dev = udev_monitor_receive_device(mon);
            if (dev && is_parent_usb(dev)) {
                printf("Got Device\n");
                printf("   Node: %s\n", udev_device_get_devnode(dev));
                printf("   Subsystem: %s\n", udev_device_get_subsystem(dev));
                printf("   Devtype: %s\n", udev_device_get_devtype(dev));

                printf("   Action: %s\n",udev_device_get_action(dev));
                udev_device_unref(dev);
            }
            else if (!dev) {
                printf("No Device from receive_device(). An error occured.\n");
            }                   
        }
        usleep(250*1000);
        printf(".");
        fflush(stdout);
    }

    udev_unref(udev);
}


int main (void)
{
    for (auto device : enumerate()) {
        cout << "Device Node Path: " << device << endl;
    }

    monitor();

    return 0;       
}
