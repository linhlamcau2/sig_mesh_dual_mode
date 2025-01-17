/********************************************************************************************************
 * @file     RemoteElementCell.h
 *
 * @brief    A concise description.
 *
 * @author   Telink, 梁家誌
 * @date     2021/8/10
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

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface RemoteElementCell : UITableViewCell
@property (weak, nonatomic) IBOutlet UITextField *eleAdrTF;
@property (weak, nonatomic) IBOutlet UITextField *modelTF;
@property (weak, nonatomic) IBOutlet UITextField *pubAdrTF;
@property (weak, nonatomic) IBOutlet UIButton *sendButton;
@property (weak, nonatomic) IBOutlet UIView *bgView;

@property (nonatomic, strong) UIViewController *vc;
@end

NS_ASSUME_NONNULL_END
