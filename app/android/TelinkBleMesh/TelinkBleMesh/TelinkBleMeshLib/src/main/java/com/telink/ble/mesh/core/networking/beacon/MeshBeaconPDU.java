/********************************************************************************************************
 * @file MeshBeaconPDU.java
 *
 * @brief for TLSR chips
 *
 * @author telink
 * @date Sep. 30, 2017
 *
 * @par Copyright (c) 2017, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *******************************************************************************************************/
package com.telink.ble.mesh.core.networking.beacon;

import com.telink.ble.mesh.core.provisioning.pdu.PDU;


/**
 * This abstract class represents a Mesh Beacon PDU (Protocol Data Unit).
 * It implements the PDU interface.
 * The Mesh Beacon PDU is used in a mesh network to transmit various types of beacons.
 */
public abstract class MeshBeaconPDU implements PDU {

    /**
     * The beacon type for an unprovisioned device.
     */
    public static final byte BEACON_TYPE_UNPROVISIONED_DEVICE = 0x00;

    /**
     * The beacon type for a secure network.
     */
    public static final byte BEACON_TYPE_SECURE_NETWORK = 0x01;

    /**
     * The beacon type for a mesh private beacon.
     */
    public static final byte BEACON_TYPE_MESH_PRIVATE = 0x02;

    /**
     * The beacon type of this MeshBeaconPDU.
     */
    protected byte beaconType;

    /**
     * The beacon data of this MeshBeaconPDU.
     */
    protected byte[] beaconData;

}