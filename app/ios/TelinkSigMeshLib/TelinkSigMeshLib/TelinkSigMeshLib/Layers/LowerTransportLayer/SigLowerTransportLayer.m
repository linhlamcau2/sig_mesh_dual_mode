/********************************************************************************************************
 * @file     SigLowerTransportLayer.m
 *
 * @brief    for TLSR chips
 *
 * @author   Telink, 梁家誌
 * @date     2019/9/16
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

#import "SigLowerTransportLayer.h"
#import "SigSegmentAcknowledgmentMessage.h"
#import "SigNetworkLayer.h"
#import "SigUpperTransportPdu.h"
#import "SigSegmentedAccessMessage.h"
#import "SigAccessMessage.h"
#import "SigSegmentedMessage.h"
#import "SigUpperTransportLayer.h"
#import "SigControlMessage.h"
#import "SigSegmentedControlMessage.h"
#import "SigNetworkManager.h"

/*
 The lower transport layer defines how upper transport layer messages are segmented and reassembled
 into multiple Lower Transport PDUs to deliver large upper transport layer messages to other nodes.
 It also defines a single control message to manage segmentation and reassembly.
 */
@interface SigLowerTransportLayer ()
@property (nonatomic,strong) dispatch_queue_t mutex;

@end

@implementation SigLowerTransportLayer

- (instancetype)initWithNetworkManager:(SigNetworkManager *)networkManager {
    /// Use the init method of the parent class to initialize some properties of the parent class of the subclass instance.
    if (self = [super init]) {
        /// Initialize self.
        _networkManager = networkManager;
        self.incompleteSegments = [NSMutableDictionary dictionary];
        self.incompleteTimers = [NSMutableDictionary dictionary];
        self.acknowledgmentTimers = [NSMutableDictionary dictionary];
        self.outgoingSegments = [NSMutableDictionary dictionary];
        self.segmentTransmissionTimers = [NSMutableDictionary dictionary];
        self.acknowledgments = [NSMutableDictionary dictionary];
        self.segmentTtl = [NSMutableDictionary dictionary];
        _mutex = dispatch_queue_create("mutex", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

/// This method handles the received Network PDU. If needed, it will reassembly
/// the message, send block acknowledgment to the sender, and pass the Upper
/// Transport PDU to the Upper Transport Layer.
///
/// - parameter networkPdu: The Network PDU received.
- (void)handleNetworkPdu:(SigNetworkPdu *)networkPdu {
    TelinkLogVerbose(@"receive:%@,%@,%d",networkPdu,networkPdu.pduData,networkPdu.isSegmented);
    @synchronized(self) {
        // Some validation, just to be sure. This should pass for sure.
        if (networkPdu.transportPdu.length <= 1) {
            return;
        }
        // Segmented messages must be validated and assembled in thread safe way.
        __weak typeof(self) weakSelf = self;
        dispatch_async(_mutex, ^{
            BOOL result = [weakSelf checkAgainstReplayAttackWithNetworkPdu:networkPdu];
            if (!result) {
                TelinkLogError(@"LowerTransportError.replayAttack");
                return;
            }

            if (networkPdu.isSegmented == YES) {
                //isReceiveSegmentPDUing=YES时，只接收sequenceNumber合法的ack包和networkPdu.isSegmented=YES的包
                if (SigMeshLib.share.isReceiveSegmentPDUing) {
                    if (SigMeshLib.share.sourceOfReceiveSegmentPDU != networkPdu.source) {
                        TelinkLogError(@"LowerTransportError.busy");
                        [weakSelf sendBusySegmentAcknowledgmentMessageWithNetworkPdu:networkPdu];
                        return;
                    }
                }
            }

            // Lower Transport Messages can be Unsegmented or Segmented.
            // This information is stored in the most significant bit of the first octet.
            BOOL segmented = networkPdu.isSegmented;
//            TelinkLogInfo(@"==========networkPdu.isSegmented=%d,networkPdu.type=%d",networkPdu.isSegmented,networkPdu.type);
            if (segmented) {
                if (networkPdu.ctl == SigLowerTransportPduType_accessMessage) {
                    SigSegmentedAccessMessage *segment = [[SigSegmentedAccessMessage alloc] initFromSegmentedPdu:networkPdu];
                    if (segmented) {
//                        TelinkLogInfo(@"accessMessage %@ received (decrypted using key: %@)",segment,segment.networkKey);
                        SigLowerTransportPdu *pdu = [weakSelf assembleSegmentedMessage:segment createdFrom:networkPdu];
                        if (pdu) {
                            [SigMeshLib.share cleanReceiveSegmentBusyStatus];
                            [weakSelf.networkManager.upperTransportLayer handleLowerTransportPdu:pdu];
                        }
//                        else {
//                            [SigMeshLib.share receiveNetworkPdu:networkPdu];
//                        }
                    }else{
                        TelinkLogError(@"segmented = nil.");
                    }
                } else if (networkPdu.ctl == SigLowerTransportPduType_transportControlMessage) {
                    SigSegmentedControlMessage *segment = [[SigSegmentedControlMessage alloc] initFromSegmentedPdu:networkPdu];
                    if (segmented) {
//                        TelinkLogInfo(@"controlMessage %@ received (decrypted using key: %@)",segment,segment.networkKey);
                        SigLowerTransportPdu *pdu = [weakSelf assembleSegmentedMessage:segment createdFrom:networkPdu];
                        if (pdu) {
                            [SigMeshLib.share cleanReceiveSegmentBusyStatus];
                            [weakSelf.networkManager.upperTransportLayer handleLowerTransportPdu:pdu];
                        }
//                        else {
//                            [SigMeshLib.share receiveNetworkPdu:networkPdu];
//                        }
                    } else {
                        TelinkLogError(@"segmented = nil.");
                    }
                }else{
                    TelinkLogError(@"networkPdu.type no exist.");
                }
            }else{
                if (networkPdu.ctl == SigLowerTransportPduType_accessMessage) {
                    SigAccessMessage *accessMessage = [[SigAccessMessage alloc] initFromUnsegmentedPdu:networkPdu];
                    if (accessMessage) {
//                        TelinkLogVerbose(@"%@ received (decrypted using key: %@)",accessMessage,accessMessage.networkKey);
                        // Unsegmented message is not acknowledged. Just pass it to higher layer.
                        [weakSelf.networkManager.upperTransportLayer handleLowerTransportPdu:accessMessage];
                    } else {
                        TelinkLogError(@"accessMessage = nil.");
                    }
                }else if (networkPdu.ctl == SigLowerTransportPduType_transportControlMessage) {
                    UInt8 tem = 0;
                    Byte *byte = (Byte *)networkPdu.transportPdu.bytes;
                    memcpy(&tem, byte, 1);
                    UInt8 opCode = tem & 0x7F;
                    if (opCode == 0x00) {
                        SigSegmentAcknowledgmentMessage *ack = [[SigSegmentAcknowledgmentMessage alloc] initFromNetworkPdu:networkPdu];
                        if (ack) {
//                            TelinkLogVerbose(@"SigSegmentAcknowledgmentMessage received =%@ (decrypted using key: %@)",ack,ack.networkKey);
                            [weakSelf handleSegmentAcknowledgmentMessage:ack];
                        } else {
                            TelinkLogError(@"ack = nil.");
                        }
                    } else {
                        SigControlMessage *controlMessage = [[SigControlMessage alloc] initFromNetworkPdu:networkPdu];
                        if (controlMessage) {
                            TelinkLogVerbose(@"%@ received (decrypted using key: %@)",controlMessage,controlMessage.networkKey);
                            // Unsegmented message is not acknowledged. Just pass it to higher layer.
                            [weakSelf.networkManager.upperTransportLayer handleLowerTransportPdu:controlMessage];
                        } else {
                            TelinkLogError(@"controlMessage = nil.");
                        }
                    }
                }else{
                    TelinkLogError(@"networkPdu.type no exist.");
                }
            }
        });
    }
}

- (void)sendBusySegmentAcknowledgmentMessageWithNetworkPdu:(SigNetworkPdu *)networkPdu {
    SigSegmentAcknowledgmentMessage *ack = [[SigSegmentAcknowledgmentMessage alloc] initBusySegmentAcknowledgmentMessageWithNetworkPdu:networkPdu];
    dispatch_async(SigMeshLib.share.queue, ^{
        TelinkLogInfo(@"sending busy ACK=%@ ,from ack.source :0x%x, to destination :0x%x, ack.sequenceZero=0x%x, ======sourceOfReceiveSegmentPDU=0x%x",ack,ack.source,ack.destination,ack.sequenceZero, SigMeshLib.share.sourceOfReceiveSegmentPDU);
        [self.networkManager.networkLayer sendLowerTransportPdu:ack ofType:SigPduType_networkPdu withTtl:SigMeshLib.share.defaultTtl ivIndex:SigMeshLib.share.dataSource.curNetkeyModel.ivIndex];
    });
}

/// This method tries to send the Upper Transport Message.
/// - Parameters:
///   - pdu:        The unsegmented Upper Transport PDU to be sent.
///   - initialTtl: The initial TTL (Time To Live) value of the message.
///                 If `nil`, the default Node TTL will be used.
///   - networkKey: The Network Key to be used to encrypt the message on
///                 on Network Layer.
///   - ivIndex: The ivIndex of mrdh network.
- (void)sendUnsegmentedUpperTransportPdu:(SigUpperTransportPdu *)pdu withTtl:(UInt8)initialTtl usingNetworkKey:(SigNetkeyModel *)networkKey ivIndex:(SigIvIndex *)ivIndex {
    SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
    if (provisionerNode == nil) {
        TelinkLogError(@"curLocationNodeModel = nil.");
        return;
    }
    UInt8 ttl = initialTtl;
    if (![SigHelper.share isRelayedTTL:ttl]) {
        ttl = provisionerNode.defaultTTL;
        if (![SigHelper.share isRelayedTTL:ttl]) {
            ttl = _networkManager.defaultTtl;
        }
    }
    SigAccessMessage *message = [[SigAccessMessage alloc] initFromUnsegmentedUpperTransportPdu:pdu usingNetworkKey:networkKey];
    _networkManager.lowerTransportLayer.unSegmentLowerTransportPdu = message;
    [_networkManager.networkLayer sendLowerTransportPdu:message ofType:SigPduType_networkPdu withTtl:ttl ivIndex:ivIndex];
    [_networkManager notifyAboutDeliveringMessage:pdu.message fromLocalElement:pdu.localElement toDestination:pdu.destination];
}

/// This method tries to send the Upper Transport Message.
/// - Parameters:
///   - pdu: The segmented Upper Transport PDU to be sent.
///   - initialTtl: The initial TTL (Time To Live) value of the message.
///                         If `nil`, the default Node TTL will be used.
///   - networkKey: The Network Key to be used to encrypt the message on
///                         on Network Layer.
///   - ivIndex: The ivIndex of mrdh network.
- (void)sendSegmentedUpperTransportPdu:(SigUpperTransportPdu *)pdu withTtl:(UInt8)initialTtl usingNetworkKey:(SigNetkeyModel *)networkKey ivIndex:(SigIvIndex *)ivIndex {
    SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
    if (provisionerNode == nil) {
        TelinkLogError(@"curLocationNodeModel = nil.");
        return;
    }
    /// Last 13 bits of the sequence number are known as seqZero.
    UInt16 sequenceZero = (UInt16)(pdu.sequence & 0x1FFF);
    //==========telink need this==========//
    if ([SigHelper.share isUnicastAddress:pdu.destination]) {
        [self startTXTimeoutWithAddress:pdu.destination sequenceZero:sequenceZero];
    }
    //==========telink need this==========//
    /// Number of segments to be sent.
    NSInteger count = (pdu.transportPdu.length + pdu.segmentedMessageLowerTransportPDUMaxLength - 1) / pdu.segmentedMessageLowerTransportPDUMaxLength;
    // Create all segments to be sent.
    NSMutableArray *outgoingSegments = [NSMutableArray array];
    for (int i=0; i<count; i++) {
        SigSegmentedAccessMessage *msg = [[SigSegmentedAccessMessage alloc] initFromUpperTransportPdu:pdu usingNetworkKey:networkKey ivIndex:ivIndex offset:(UInt8)i];
        [outgoingSegments addObject:msg];
    }
    UInt8 ttl = initialTtl;
    if (![SigHelper.share isRelayedTTL:ttl] || ttl == 0) {
        ttl = provisionerNode.defaultTTL;
        if (![SigHelper.share isRelayedTTL:ttl]) {
            ttl = _networkManager.defaultTtl;
        }
    }
    _segmentTtl[@(sequenceZero)] = @(ttl);
    _outgoingSegments[@(sequenceZero)] = outgoingSegments;
    [self sendSegmentsForSequenceZero:sequenceZero limit:_networkManager.retransmissionLimit];
}

- (void)cancelTXSendingSegmentedWithDestination:(UInt16)destination {
    if (_incompleteTimers && _incompleteTimers.allKeys.count > 0) {
        NSArray *keys = _incompleteTimers.allKeys;
        for (NSNumber *n in keys) {
            UInt32 key = n.intValue;
            if (((key >> 16) & 0xFFFF) == destination) {
                BackgroundTimer *t = _incompleteTimers[n];
                [t invalidate];
                [_incompleteTimers removeObjectForKey:n];
                t = nil;
                break;
            }
        }
    }
}

/// Cancels sending segmented Upper Transoprt PDU.
///
/// - parameter pdu: The Upper Transport PDU.
- (void)cancelSendingSegmentedUpperTransportPdu:(SigUpperTransportPdu *)pdu {
    /// Last 13 bits of the sequence number are known as seqZero.
    UInt16 sequenceZero = (UInt16)(pdu.sequence & 0x1FFF);
    TelinkLogInfo(@"Cancelling sending segments with seqZero:0x%X",sequenceZero);
    [_outgoingSegments removeObjectForKey:@(sequenceZero)];
    [_segmentTtl removeObjectForKey:@(sequenceZero)];
    BackgroundTimer *timer = _segmentTransmissionTimers[@(sequenceZero)];
    if (timer) {
        [timer invalidate];
    }
    [_segmentTransmissionTimers removeObjectForKey:@(sequenceZero)];
    timer = nil;
}

#pragma mark - private

- (BOOL)checkAgainstReplayAttackWithNetworkPdu:(SigNetworkPdu *)networkPdu {
    //v3.3.3.6及之后防重放攻击逻辑：isSegmented则比较seqZero，非isSegmented则比较sequenceNumber。
    SigNodeSequenceNumberCacheModel *nodeSequenceNumberCacheModel = [SigMeshLib.share.dataSource getSigNodeSequenceNumberCacheModelWithUnicastAddress:networkPdu.source];
    UInt64 receiveSequence = (UInt64)networkPdu.sequence;
    UInt64 receiveIvIndex = (UInt64)networkPdu.getDecodeIvIndex;
    UInt64 receivedSeqAuth = (receiveIvIndex << 24) | receiveSequence;
    UInt16 receivedSeqZero = 0;
    if (networkPdu.isSegmented) {
        NSData *data = networkPdu.transportPdu;
        Byte *dataByte = (Byte *)data.bytes;
        UInt16 tem1=0,tem2=0;
        memcpy(&tem1, dataByte+1, 1);
        memcpy(&tem2, dataByte+2, 1);
        receivedSeqZero = (UInt16)(((tem1 & 0x7F) << 6) | (UInt16)(tem2 >> 2));
    }

    BOOL newSource = nodeSequenceNumberCacheModel == nil;//source为node的address
    if (!newSource) {
        NSNumber *lastSeqAuthNumber = nodeSequenceNumberCacheModel.seqAuth;
        UInt64 lastSeqAuth = lastSeqAuthNumber.intValue;
        if (networkPdu.isSegmented) {
            if (nodeSequenceNumberCacheModel.seqZero != nil) {
                // get cache of seqZero
                SigNodeSeqZeroModel *seqZero = nodeSequenceNumberCacheModel.seqZero;
                NSNumber *lastSeqZeroValue = seqZero.value;
                NSMutableArray *lastSeqAuths = seqZero.seqAuths;
                if (lastSeqZeroValue.intValue == receivedSeqZero && lastSeqAuths.count == 0) {
                    TelinkLogError(@"Discarding packet (lastSeqZeroValue:0x%X, receivedSeqZero >0x%X, lastSeqAuths=%@)", lastSeqZeroValue.intValue, receivedSeqZero, lastSeqAuths);
                    return NO;
                }
                if (lastSeqZeroValue.intValue > receivedSeqZero) {
                    TelinkLogError(@"Discarding packet (lastSeqZeroValue:0x%X, receivedSeqZero >0x%X)", lastSeqZeroValue.intValue, receivedSeqZero);
                    return NO;
                } else if (lastSeqZeroValue.intValue < receivedSeqZero) {
                    [lastSeqAuths removeAllObjects];
                }
                if ([lastSeqAuths containsObject:@(receivedSeqAuth)]) {
                    TelinkLogError(@"Discarding packet (lastSeqAuths:%@, receivedSeqAuth >0x%X)", lastSeqAuths, receivedSeqAuth);
                    return NO;
                }
                [lastSeqAuths addObject:@(receivedSeqAuth)];
            } else {
                // add cache of seqZero
                if (lastSeqAuth > receivedSeqAuth) {
                    TelinkLogError(@"Discarding packet (lastSeqAuth:0x%X, receivedSeqAuth >0x%X)", lastSeqAuth, receivedSeqAuth);
                    return NO;
                }
                SigNodeSeqZeroModel *seqZero = [[SigNodeSeqZeroModel alloc] initWithValue:@(receivedSeqZero) seqAuth:@(receivedSeqAuth)];
                nodeSequenceNumberCacheModel.seqZero = seqZero;
            }
        } else {
            if (lastSeqAuth > receivedSeqAuth) {
                TelinkLogError(@"Discarding packet (lastSeqAuth:0x%X, receivedSeqAuth >0x%X)", lastSeqAuth, receivedSeqAuth);
                return NO;
            }
            nodeSequenceNumberCacheModel.seqAuth = @(receivedSeqAuth);
            nodeSequenceNumberCacheModel.seqZero = nil;
        }
    } else {
        nodeSequenceNumberCacheModel = [[SigNodeSequenceNumberCacheModel alloc] init];
        nodeSequenceNumberCacheModel.unicastAddress = networkPdu.source;
        nodeSequenceNumberCacheModel.seqAuth = @(receivedSeqAuth);
        if (networkPdu.isSegmented) {
            SigNodeSeqZeroModel *seqZero = [[SigNodeSeqZeroModel alloc] init];
            seqZero.value = @(receivedSeqZero);
            nodeSequenceNumberCacheModel.seqZero = seqZero;
            if (networkPdu.source != SigMeshLib.share.sourceOfReceiveSegmentPDU && SigMeshLib.share.sourceOfReceiveSegmentPDU != 0) {
                //busy的ack
            } else {
                //不busy
                [nodeSequenceNumberCacheModel.seqZero.seqAuths addObject:@(receivedSeqAuth)];
            }
        }
        [SigMeshLib.share.dataSource.nodeSequenceNumberCacheList addObject:nodeSequenceNumberCacheModel];
    }
    return YES;
}

/// Handles the segment created from the given network PDU.
/// @param segment The segment to handle.
/// @param networkPdu The Network PDU from which the segment was decoded.
/// @returns The Lower Transport PDU had it been fully assembled, `nil` otherwise.
- (SigLowerTransportPdu *)assembleSegmentedMessage:(SigSegmentedMessage *)segment createdFrom:(SigNetworkPdu *)networkPdu {
    // If the received segment comes from an already completed and
    // acknowledged message, send the same ACK immediately.
    SigSegmentAcknowledgmentMessage *lastAck = _acknowledgments[@(segment.source)];
    if (lastAck && lastAck.sequenceZero == segment.sequenceZero) {
        SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
        if (provisionerNode) {
            TelinkLogInfo(@"Message already acknowledged, sending ACK again.");
            UInt8 ttl = 0;
            if (networkPdu.ttl > 0) {
                ttl = provisionerNode.defaultTTL;
                if (![SigHelper.share isRelayedTTL:ttl]) {
                    ttl = _networkManager.defaultTtl;
                }
            }
            //解决接收完一组完整的segment后，可能由于网络原因还接收到重复的segment，导致后续的segment接收异常的问题。
            if (networkPdu.source == SigMeshLib.share.sourceOfReceiveSegmentPDU) {
                [SigMeshLib.share cleanReceiveSegmentBusyStatus];
            }
            [self sendAckSigSegmentAcknowledgmentMessage:lastAck withTtl:ttl];
        } else {
            [_acknowledgments removeObjectForKey:@(segment.source)];
        }
        return nil;
    }
    // Remove the last ACK. The source Node has sent a new message, so
    // the last ACK must have been received.
    [_acknowledgments removeObjectForKey:@(segment.source)];

    // A segmented message may be composed of 1 or more segments.
    if (segment.isSingleSegment) {
        SigLowerTransportPdu *message = nil;
        if ([segment isKindOfClass:[SigSegmentedAccessMessage class]]) {
            message = [[SigAccessMessage alloc] initFromSegments:@[(SigSegmentedAccessMessage *)segment]];
        } else {
            message = [[SigControlMessage alloc] initFromSegments:@[(SigSegmentedAccessMessage *)segment]];
        }
        // A single segment message may immediately be acknowledged.
        SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
        if (provisionerNode == nil) {
            TelinkLogError(@"curLocationNodeModel = nil.");
            return nil;
        }
        if (networkPdu.destination == provisionerNode.address) {
            UInt8 ttl = 0;
            if (networkPdu.ttl > 0) {
                ttl = provisionerNode.defaultTTL;
                if (![SigHelper.share isRelayedTTL:ttl]) {
                    ttl = _networkManager.defaultTtl;
                }
            }
            TelinkLogInfo(@"response last segment, sent ack.");
            [self sendAckForSegments:@[segment] withTtl:ttl];
        }
        message.networkPduModel = networkPdu;
        return message;
    }else{
        // If a message is composed of multiple segments, they all need to
        // be received before it can be processed.
        UInt32 key = (UInt32)[self getKeyForAddress:networkPdu.source sequenceZero:segment.sequenceZero];
        if (_incompleteSegments[@(key)] == nil) {
            _incompleteSegments[@(key)] = [NSMutableArray array];
            for (int i=0; i<segment.count; i++) {
                [_incompleteSegments[@(key)] addObject:(SigSegmentedMessage *)[NSNull null]];
            }
        }
        if (_incompleteSegments[@(key)].count <= segment.index) {
            // Segment is invalid. We can stop here.
            TelinkLogInfo(@"Invalid segment.");
            return nil;
        }
        _incompleteSegments[@(key)][segment.index] = segment;

        // If all segments were received, send ACK and send the PDU to Upper
        // Transport Layer for processing.
        if ([self segmentsArrayIsComplete:_incompleteSegments[@(key)]]) {
            NSMutableArray *allSegments = _incompleteSegments[@(key)];
            [_incompleteSegments removeObjectForKey:@(key)];
            SigLowerTransportPdu *message = nil;
            SigSegmentedMessage *seg = [self firstNotAcknowledgedFrom:allSegments];
            if ([seg isKindOfClass:[SigSegmentedAccessMessage class]]) {
                message = [[SigAccessMessage alloc] initFromSegments:(NSArray<SigSegmentedAccessMessage *> *)allSegments];
            } else {
                message = [[SigControlMessage alloc] initFromSegments:(NSArray<SigSegmentedAccessMessage *> *)allSegments];
            }
//            TelinkLogInfo(@"%@ received",message);
            // If the access message was targeting directly the local Provisioner...
            SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
            if (provisionerNode == nil) {
                TelinkLogError(@"curLocationNodeModel = nil.");
                return nil;
            }
            if (networkPdu.destination == provisionerNode.address) {
                // ...invalidate timers...
                BackgroundTimer *timer1 = [_incompleteTimers objectForKey:@(key)];
                if (timer1) {
                    [timer1 invalidate];
                }
                [_incompleteTimers removeObjectForKey:@(key)];
                BackgroundTimer *timer2 = [_acknowledgmentTimers objectForKey:@(key)];
                if (timer2) {
                    [timer2 invalidate];
                }
                [_acknowledgmentTimers removeObjectForKey:@(key)];

                // ...and send the ACK that all segments were received.
                UInt8 ttl = 0;
                if (networkPdu.ttl > 0) {
                    ttl = provisionerNode.defaultTTL;
                    if (![SigHelper.share isRelayedTTL:ttl]) {
                        ttl = _networkManager.defaultTtl;
                    }
                }
                [self sendAckForSegments:allSegments withTtl:ttl];
            }
            message.networkPduModel = networkPdu;
            return message;
        } else {
            // The Provisioner shall send block acknowledgment only if the message was
            // send directly to it's Unicast Address.
            SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
            if (provisionerNode == nil) {
                TelinkLogError(@"curLocationNodeModel = nil.");
                return nil;
            }
            if (networkPdu.destination != provisionerNode.address) {
//                TelinkLogDebug(@"networkPdu.destination != provisionerNode.address");
                return nil;
            }
            __weak typeof(self) weakSelf = self;



            // If the Lower Transport Layer receives any segment while the acknowledgment
            // timer is inactive, it shall restart the timer. Active timer should not be restarted.
            if (_acknowledgmentTimers[@(key)] == nil) {
                UInt8 ttl = provisionerNode.defaultTTL;
                if (![SigHelper.share isRelayedTTL:ttl]) {
                    ttl = _networkManager.defaultTtl;
                }

                BackgroundTimer *timer3 = [BackgroundTimer scheduledTimerWithTimeInterval:[_networkManager acknowledgmentTimerInterval:ttl] repeats:NO block:^(BackgroundTimer * _Nonnull t) {
                    if (weakSelf.incompleteSegments[@(key)] != nil) {
                        NSMutableArray *segments = weakSelf.incompleteSegments[@(key)];
                        UInt8 ttl2 = networkPdu.ttl > 0 ? ttl : 0;
                        [weakSelf sendAckForSegments:segments withTtl:ttl2];
                    }
                    BackgroundTimer *timer4 = [weakSelf.acknowledgmentTimers objectForKey:@(key)];
                    if (timer4) {
                        [timer4 invalidate];
                    }
                    [weakSelf.acknowledgmentTimers removeObjectForKey:@(key)];
                }];
                _acknowledgmentTimers[@(key)] = timer3;
            }
            return nil;
        }
    }
}

- (void)startTXTimeoutWithAddress:(UInt16)address sequenceZero:(UInt16)sequenceZero {
    UInt32 key = (UInt32)[self getKeyForAddress:address sequenceZero:sequenceZero];
    // If the Lower Transport Layer receives any segment while the incomplete
    // timer is active, the timer shall be restarted.
    BackgroundTimer *timer1 = [_incompleteTimers objectForKey:@(key)];
    if (timer1) {
        [timer1 invalidate];
    }
    __weak typeof(self) weakSelf = self;
    BackgroundTimer *timer2 = [BackgroundTimer scheduledTimerWithTimeInterval:_networkManager.incompleteMessageTimeout repeats:NO block:^(BackgroundTimer * _Nonnull t) {
        TelinkLogDebug(@"Incomplete message timeout: cancelling message (src: 0x%x, seqZero: 0x%x)",(UInt16)(key >> 16),(UInt16)(key & 0x1FFF));
        BackgroundTimer *timer1 = [weakSelf.incompleteTimers objectForKey:@(key)];
        if (timer1) {
            [timer1 invalidate];
        }
        [weakSelf.incompleteTimers removeObjectForKey:@(key)];
        BackgroundTimer *timer2 = [weakSelf.acknowledgmentTimers objectForKey:@(key)];
        if (timer2) {
            [timer2 invalidate];
        }
        [weakSelf.acknowledgmentTimers removeObjectForKey:@(key)];
        [weakSelf.incompleteSegments removeObjectForKey:@(key)];

        // A limit has been reached and some segments were not ACK.
        NSArray *segments = weakSelf.outgoingSegments[@(sequenceZero)];
        SigSegmentedMessage *segment = [weakSelf firstNotAcknowledgedFrom:segments];
        if (segment) {
            if (segment.userInitiated && !segment.message.isAcknowledged) {
                [weakSelf.networkManager notifyAboutError:[NSError errorWithDomain:@"LowerTransportError.timeout" code:SigLowerTransportError_timeout userInfo:nil] duringSendingMessage:segment.message fromLocalElement:segment.localElement toDestination:segment.destination];
            }
            [weakSelf.networkManager.upperTransportLayer lowerTransportLayerDidSendSegmentedUpperTransportPduToDestination:segment.destination];
        }
        [weakSelf.outgoingSegments removeObjectForKey:@(sequenceZero)];
    }];
    _incompleteTimers[@(key)] = timer2;
}

/// This method handles the Segment Acknowledgment Message.
/// @param ack The Segment Acknowledgment Message received.
- (void)handleSegmentAcknowledgmentMessage:(SigSegmentAcknowledgmentMessage *)ack {
    // 先判断发送的Segment字典内是否有对应这个ack.sequenceZero的SegmentedMessage，且这个SegmentedMessage未被设备返回ack
    SigSegmentedMessage *segment = [self firstNotAcknowledgedFrom:_outgoingSegments[@(ack.sequenceZero)]];
    if (segment == nil) {
        TelinkLogError(@"No local SigSegmentedMessage of ack.sequenceZero=(0x%X),ack=%@",ack.sequenceZero,ack);
        return;
    }
    // 取消这个SegmentedMessage的Transmission定时器
    BackgroundTimer *timer = _segmentTransmissionTimers[@(ack.sequenceZero)];
    if (timer) {
        [timer invalidate];
    }
    [_segmentTransmissionTimers removeObjectForKey:@(ack.sequenceZero)];
    timer = nil;
    // Is the target Node busy?
    if (ack.isBusy) {
        [_outgoingSegments removeObjectForKey:@(ack.sequenceZero)];
        if (segment.userInitiated && !segment.message.isAcknowledged) {
            [_networkManager notifyAboutError:[NSError errorWithDomain:@"LowerTransportError.busy" code:SigLowerTransportError_busy userInfo:nil] duringSendingMessage:segment.message fromLocalElement:segment.localElement toDestination:segment.destination];
        }
        return;
    }
    // Clear all acknowledged segments.
    for (int index=0; index<_outgoingSegments[@(ack.sequenceZero)].count; index++) {
        if ([ack isSegmentReceived:index]) {
            _outgoingSegments[@(ack.sequenceZero)][index] = (SigSegmentedMessage *)[NSNull null];
        }
    }
    // If all the segments were acknowledged, notify the manager.
    if ([self segmentsArrayHasMore:_outgoingSegments[@(ack.sequenceZero)]] == NO) {
        TelinkLogInfo(@"node response SegmentAcknowledgmentMessage,all the segments were acknowledged. ack.sequenceZero = 0x%x, ack.blockAck=0x%x",ack.sequenceZero,ack.blockAck);
        UInt32 key = (UInt32)[self getKeyForAddress:ack.source sequenceZero:ack.sequenceZero];
        BackgroundTimer *timer1 = [self.incompleteTimers objectForKey:@(key)];
        if (timer1) {
            [timer1 invalidate];
        }
        [self.incompleteTimers removeObjectForKey:@(key)];

        [_outgoingSegments removeObjectForKey:@(ack.sequenceZero)];
        [_networkManager notifyAboutDeliveringMessage:segment.message fromLocalElement:segment.localElement toDestination:segment.destination];
        [_networkManager.upperTransportLayer lowerTransportLayerDidSendSegmentedUpperTransportPduToDestination:segment.destination];
    }else{
        TelinkLogInfo(@"node response SegmentAcknowledgmentMessage,send again all packets that were not acknowledged.ack.blockAck=0x%x",ack.blockAck);
        // Else, send again all packets that were not acknowledged.
        [self sendSegmentsForSequenceZero:ack.sequenceZero limit:_networkManager.retransmissionLimit];
    }
}

/// This method tries to send the Segment Acknowledgment Message to the given address. It will try to send if the local Provisioner is set and has the Unicast Address assigned.
///
/// If the `transporter` throws an error during sending, this error will be ignored.
///
/// @param segments The array of message segments, of which at least one has to be not `nil`.
/// @param ttl Initial Time To Live (TTL) value.
- (void)sendAckForSegments:(NSArray <SigSegmentedMessage *>*)segments withTtl:(UInt8)ttl {
    SigSegmentAcknowledgmentMessage *ack = [[SigSegmentAcknowledgmentMessage alloc] initForSegments:[NSArray arrayWithArray:segments]];
    ack.ivIndex = SigMeshLib.share.dataSource.curNetkeyModel.ivIndex;
    ack.networkKey = SigMeshLib.share.dataSource.curNetkeyModel;
    if ([self segmentsArrayIsComplete:segments]) {
        _acknowledgments[@(ack.destination)] = ack;
    }
    [self sendAckSigSegmentAcknowledgmentMessage:ack withTtl:ttl];
}

/// Sends the given ACK on the global background queue.
/// @param ack The Segment Acknowledgment Message to sent.
/// @param ttl Initial Time To Live (TTL) value.
- (void)sendAckSigSegmentAcknowledgmentMessage:(SigSegmentAcknowledgmentMessage *)ack withTtl:(UInt8)ttl {
//    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
//        TelinkLogInfo(@"sending ACK=%@ ,from ack.source :0x%x, to destination :0x%x, ack.sequenceZero=0x%x",ack,ack.source,ack.destination,ack.sequenceZero);
//        [self.networkManager.networkLayer sendLowerTransportPdu:ack ofType:SigPduType_networkPdu withTtl:ttl];
//    });
    dispatch_async(SigMeshLib.share.queue, ^{
        TelinkLogInfo(@"sending ACK=%@ ,from ack.source :0x%x, to destination :0x%x, ack.sequenceZero=0x%x",ack,ack.source,ack.destination,ack.sequenceZero);
        [self.networkManager.networkLayer sendLowerTransportPdu:ack ofType:SigPduType_networkPdu withTtl:ttl  ivIndex:SigMeshLib.share.dataSource.curNetkeyModel.ivIndex];
    });
}

/// Sends all non-`nil` segments from `outgoingSegments` map from the given `sequenceZero` key.
///
/// @param sequenceZero  The key to get segments from the map.
/// @param limit  retry count of segment.
- (void)sendSegmentsForSequenceZero:(UInt16)sequenceZero limit:(int)limit {
//    TelinkLogVerbose(@"sequenceZero=0x%x,limit=%d",sequenceZero,limit);
    NSArray *array = _outgoingSegments[@(sequenceZero)];
    NSInteger count = array.count;
    UInt8 ttl = (UInt8)[_segmentTtl[@(sequenceZero)] intValue];
    SigNodeModel *provisionerNode = SigMeshLib.share.dataSource.curLocationNodeModel;
    if (count == 0 || provisionerNode == nil) {
        return;
    }
    /// Segment Acknowledgment Message is expected when the message is targeting
    /// a Unicast Address.
    BOOL ackExpected = NO;

    UInt16 destination = 0;
//    TelinkLogVerbose(@"==========发送seg=%@开始",array);

    // Send all the segments that have not been acknowledged yet.
    for (int i=0; i<count; i++) {
        SigSegmentedMessage *segment = array[i];
        if (![segment isEqual:[NSNull null]]) {
            if (destination == 0) {
                destination = segment.destination;
            }
            ackExpected = [SigHelper.share isUnicastAddress:segment.destination];
            [_networkManager.networkLayer sendLowerTransportPdu:segment ofType:SigPduType_networkPdu withTtl:ttl ivIndex:segment.ivIndex];
            //==========test==========//
            //因为非直连设备地址的segment包需要在mesh网络内部进行转发，且设备不一定存在ack返回。（BLOBChunkTransfer目标地址为组播地址时）
            if (segment.destination != SigMeshLib.share.dataSource.getCurrentConnectedNode.address) {
                [NSThread sleepForTimeInterval:SigMeshLib.share.maxNetworkTransmitInterval];
            }
            //==========test==========//
        }
    }
//    TelinkLogVerbose(@"==========发送seg count=%d结束",count);
    __weak typeof(self) weakSelf = self;

    //==========telink need this==========//
    if (!ackExpected && destination) {
        [weakSelf.networkManager.upperTransportLayer lowerTransportLayerDidSendSegmentedUpperTransportPduToDestination:destination];
    }
    if (!SigBearer.share.isOpen) {
        TelinkLogError(@"bearer is closed.");
        SigSegmentedMessage *segment = array.firstObject;
        [_networkManager notifyAboutError:[NSError errorWithDomain:@"SigBearer.isClose" code:SigLowerTransportError_timeout userInfo:nil] duringSendingMessage:segment.message fromLocalElement:segment.localElement toDestination:segment.destination];
        return;
    }

    BackgroundTimer *timer = [_segmentTransmissionTimers objectForKey:@(sequenceZero)];
    if (timer) {
        [timer invalidate];
    }
    [_segmentTransmissionTimers removeObjectForKey:@(sequenceZero)];
    NSArray *segments = _outgoingSegments[@(sequenceZero)];
    BOOL hasMore = [self segmentsArrayHasMore:segments];
    if (ackExpected && segments && hasMore) {
        if (limit > 0) {
            NSTimeInterval interval = [_networkManager transmissionTimerInterval:ttl];
            BackgroundTimer *timer2 = [BackgroundTimer scheduledTimerWithTimeInterval:interval repeats:NO block:^(BackgroundTimer * _Nonnull t) {
                [weakSelf sendSegmentsForSequenceZero:sequenceZero limit:limit-1];
            }];
            _segmentTransmissionTimers[@(sequenceZero)] = timer2;
        } else {
            // A limit has been reached and some segments were not ACK.
            SigSegmentedMessage *segment = [self firstNotAcknowledgedFrom:segments];
            if (segment) {
                if (segment.userInitiated && !segment.message.isAcknowledged) {
                    [_networkManager notifyAboutError:[NSError errorWithDomain:@"LowerTransportError.timeout" code:SigLowerTransportError_timeout userInfo:nil] duringSendingMessage:segment.message fromLocalElement:segment.localElement toDestination:segment.destination];
                }
                [_networkManager.upperTransportLayer lowerTransportLayerDidSendSegmentedUpperTransportPduToDestination:segment.destination];
            }
            [_outgoingSegments removeObjectForKey:@(sequenceZero)];
        }
    } else
    {
        // All segments have been successfully sent to a Group Address.
        SigSegmentedMessage *segment = [self firstNotAcknowledgedFrom:array];
        if (segment) {
            [_networkManager notifyAboutDeliveringMessage:segment.message fromLocalElement:segment.localElement toDestination:segment.destination];
        }
        [_outgoingSegments removeObjectForKey:@(sequenceZero)];
    }
}

- (void)sendSegment:(NSTimer *)timer {
    NSDictionary *userInfo = timer.userInfo;
    NSArray *segments = userInfo[@"segments"];
    NSInteger ttl = [userInfo[@"ttl"] integerValue];
    for (int i=0; i<segments.count; i++) {
        SigSegmentedMessage *segment = segments[i];
        [_networkManager.networkLayer sendLowerTransportPdu:segment ofType:SigPduType_networkPdu withTtl:ttl  ivIndex:SigMeshLib.share.dataSource.curNetkeyModel.ivIndex];
    }
    if (timer) {
        [timer invalidate];
    }
}

- (void)sendSegment2:(NSTimer *)timer {
    NSDictionary *userInfo = timer.userInfo;
    UInt16 sequenceZero = [userInfo[@"sequenceZero"] intValue];
    int limit = [userInfo[@"limit"] intValue];
    [self sendSegmentsForSequenceZero:sequenceZero limit:limit];
}

/// Returns the key used in maps in Lower Transport Layer to keep segments received to or from given source address.
- (UInt32)getKeyForAddress:(UInt16)address sequenceZero:(UInt16)sequenceZero {
    return ((UInt32)address << 16) | (UInt32)(sequenceZero & 0x1FFF);
}

/// This method tries to send the Segment Acknowledgment Message to the
/// given address. It will try to send if the local Provisioner is set and
/// has the Unicast Address assigned.
///
/// If the `transporter` throws an error during sending, this error will be ignored.
///
/// - parameter segments:   The array of message segments, of which at least one
///                         has to be not `nil`.
/// - parameter networkKey: The Network Key to be used to encrypt the message on
///                         on Network Layer.
/// - parameter ttl:        Initial Time To Live (TTL) value.
- (void)sendAckForSegments:(NSArray <SigSegmentedMessage *>*)segments usingNetworkKey:(SigNetkeyModel *)networkKey withTtl:(UInt8)ttl {
    SigSegmentAcknowledgmentMessage *ack = [[SigSegmentAcknowledgmentMessage alloc] initForSegments:[NSArray arrayWithArray:segments]];
    if ([self segmentsArrayIsComplete:segments]) {
        _acknowledgments[@(ack.destination)] = ack;
    }
    [self sendAckSigSegmentAcknowledgmentMessage:ack withTtl:ttl];
}

/// Returns whether all the segments were received.
- (BOOL)segmentsArrayIsComplete:(NSArray *)array {
    BOOL tem = NO;
    for (NSObject *obj in array) {
        if ([obj isEqual:[NSNull null]]) {
            tem = YES;
            break;
        }
    }
    return !tem;
}

/// Returns whether some segments were not yet acknowledged.
- (BOOL)segmentsArrayHasMore:(NSArray *)array {
    BOOL tem = NO;
    for (NSObject *obj in array) {
        if (![obj isEqual:[NSNull null]]) {
            tem = YES;
            break;
        }
    }
    return tem;
}

- (SigSegmentedMessage *)firstNotAcknowledgedFrom:(NSArray *)array{
    SigSegmentedMessage *segment = nil;
    for (SigSegmentedMessage *tem in array) {
        if (![tem isEqual:[NSNull null]]) {
            segment = tem;
            break;
        }
    }
    return segment;
}

//limit=10
- (void)sendSegmentsForSequenceZero:(UInt16)sequenceZero {
    [self sendSegmentsForSequenceZero:sequenceZero limit:10];
}

@end
