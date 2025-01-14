/*
 * Copyright (c) 2021 - 2022 Pedro Falcato
 * This file is part of Onyx, and is released under the terms of the MIT License
 * check LICENSE at the root directory for more information
 *
 * SPDX-License-Identifier: MIT
 */
#include "include/pci_bus.h"

#include <string.h>

#include <onyx/log.h>
#include <onyx/panic.h>

#include <pci/pci.h>

#include "include/pci_root.h"

#include <onyx/memory.hpp>

namespace pci
{

pci_device *pci_bus::enumerate_device(const device_address &addr, pcie_allocation *alloc)
{
    // printk("Device at %04x:%02x:%02x.%02x\n", addr.segment, addr.bus, addr.device,
    // addr.function);

    /* Set up the pci device's name */
    char name_buf[50] = {};
    snprintf(name_buf, 50, "%04x:%02x:%02x.%02x", addr.segment, addr.bus, addr.device,
             addr.function);

    auto vendor = (uint16_t) read_config(addr, PCI_REGISTER_VENDOR_ID, sizeof(uint16_t));
    auto device_id = (uint16_t) read_config(addr, PCI_REGISTER_DEVICE_ID, sizeof(uint16_t));
    auto dev = make_unique<pci_device>(strdup(name_buf), this, nullptr, device_id, vendor, addr);
    if (!dev)
        panic("Out of memory allocating pci device");

    dev->set_alloc(alloc);

    dev->init();

    auto raw_dev = dev.get();

    bus_add_device(this, dev.release());

    if ((raw_dev->header_type() & PCI_TYPE_MASK) == PCI_TYPE_BRIDGE)
    {
        auto nbus = (uint8_t) raw_dev->read(PCI_REGISTER_SECONDARY_BUS, sizeof(uint8_t));
        // printk("PCI-to-PCI bridge at nbus %x!\n", nbus);
        auto bus = new pci_bus{nbus, this, this->parent_root};
        if (!bus)
            panic("Out of memory allocating pci bus");

        bus->discover();

        add_bus(bus);

        pci::add_bus(bus);
    }

    return raw_dev;
}

void pci_bus::discover()
{
    device_address addr;
    addr.segment = parent_root->get_segment();
    addr.bus = nbus;
    addr.function = 0;
    addr.device = 0;
    auto alloc = get_alloc();

    for (unsigned int dev = 0; dev < PCI_NR_DEV; dev++)
    {
        addr.device = dev;
        addr.function = 0;
        // Probe it by reading the vendor ID - if there's no device present
        // there will be an abort generated by the PCI controller and we get an
        // all-ones value

        uint16_t vendor_id = (uint16_t) read_config(addr, PCI_REGISTER_VENDOR_ID, sizeof(uint16_t));

        if (vendor_id == 0xffff)
            continue;

        auto device = enumerate_device(addr, alloc);

        auto a = addr;

        auto header = (uint16_t) device->read(PCI_REGISTER_HEADER, sizeof(uint16_t));

        if (header & PCI_HEADER_MULTIFUNCTION)
        {
            for (int i = 1; i < 8; i++)
            {
                a.function = i;

                if ((uint16_t) read_config(a, PCI_REGISTER_VENDOR_ID, sizeof(uint16_t)) == 0xffff)
                    continue;

                enumerate_device(a, alloc);
            }
        }
    }
}

#ifdef CONFIG_ACPI

ACPI_STATUS pci_bus::route_bus_irqs(ACPI_HANDLE bus_object)
{
    ACPI_BUFFER buf;
    buf.Length = ACPI_ALLOCATE_BUFFER;

    // The gist of this is that we need to get an irq routing table for the root complex, and
    // not so much for the other buses, which may or may not have _PRT methods.
    if (auto st = AcpiGetIrqRoutingTable(bus_object, &buf); ACPI_FAILURE(st))
    {
        return st;
    }

    // This is all documented in ACPI spec 6.13 _PRT

    ACPI_PCI_ROUTING_TABLE *it = (ACPI_PCI_ROUTING_TABLE *) buf.Pointer;
    for (; it->Length != 0; it = (ACPI_PCI_ROUTING_TABLE *) ACPI_NEXT_RESOURCE(it))
    {
        // The format for Address is the same as in _ADR
        // Bits [0...16] -> Function
        // Bits [16...32] -> Device
        // It's also specified in 6.13 that Address' function field MUST be 0xffff,
        // which means all functions under $device, so we can safely ignore it and filter
        // for functions with address {$segment, $bus, $device, any}
        uint8_t devnum = it->Address >> 16;

        uint32_t pin = it->Pin;
        uint32_t gsi = -1;
        bool level = true;
        bool active_high = false;

        // If the first byte of the source is 0, the GSI is SourceIndex
        if (it->Source[0] == 0)
        {
            gsi = it->SourceIndex;
        }
        else
        {
            // Else, Source contains the Path of the object we need to evaluate
            // with _CRS in order to get IRQ configuration for the pin.
            ACPI_HANDLE link_obj;
            ACPI_STATUS st = AcpiGetHandle(bus_object, it->Source, &link_obj);

            if (ACPI_FAILURE(st))
            {
                ERROR("acpi", "Error while calling AcpiGetHandle: %x\n", st);
                return st;
            }

            ACPI_BUFFER rbuf;
            rbuf.Length = ACPI_ALLOCATE_BUFFER;
            rbuf.Pointer = nullptr;

            st = AcpiGetCurrentResources(link_obj, &rbuf);
            if (ACPI_FAILURE(st))
            {
                ERROR("acpi", "Error while calling AcpiGetCurrentResources: %x\n", st);
                return st;
            }

            for (ACPI_RESOURCE *res = (ACPI_RESOURCE *) rbuf.Pointer;
                 res->Type != ACPI_RESOURCE_TYPE_END_TAG; res = ACPI_NEXT_RESOURCE(res))
            {
                if (res->Type == ACPI_RESOURCE_TYPE_IRQ)
                {
                    level = res->Data.Irq.Polarity == 0;
                    active_high = res->Data.Irq.Triggering == 0;
                    gsi = res->Data.Irq.Interrupts[it->SourceIndex];
                    break;
                }
                else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
                {
                    level = res->Data.ExtendedIrq.Polarity == 0;
                    active_high = res->Data.ExtendedIrq.Triggering == 0;
                    gsi = res->Data.ExtendedIrq.Interrupts[it->SourceIndex];
                    break;
                }
            }

            free(rbuf.Pointer);
        }

        // Find every device that matches the address mentioned above, and set
        // the pin_to_gsi information.
        // TODO: This is weird and can be done without keeping information in
        // pci_device
        for_every_device([&](device *dev_) -> bool {
            auto dev = (pci_device *) dev_;

            // Skip this device if it's not our device
            if (dev->addr().device != devnum)
                return true;

            auto pin_to_gsi = dev->get_pin_to_gsi();

            pin_to_gsi[pin].active_high = active_high;
            pin_to_gsi[pin].gsi = gsi;
            pin_to_gsi[pin].level = level;

            auto addr = dev->addr();

            printf("acpi: %04x:%02x:%02x.%02x: pin INT%c ==> GSI %u\n", addr.segment, addr.bus,
                   addr.device, addr.function, 'A' + pin, gsi);

            return true;
        });
    }

    return AE_OK;
}
#endif

} // namespace pci
