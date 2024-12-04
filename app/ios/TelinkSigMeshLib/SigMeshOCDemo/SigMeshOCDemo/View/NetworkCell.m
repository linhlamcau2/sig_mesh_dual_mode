/********************************************************************************************************
 * @file     NetworkCell.m
 *
 * @brief    A concise description.
 *
 * @author   Telink, 梁家誌
 * @date     2023/5/15
 *
 * @par     Copyright (c) 2023, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

#import "NetworkCell.h"
#import "NSString+extension.h"

@implementation NetworkCell

- (void)awakeFromNib {
    [super awakeFromNib];
    // Initialization code
    //cornerRadius
    self.bgView.layer.cornerRadius = 10;
    //borderWidth
    self.bgView.layer.borderWidth = 2;
    //borderColor
    self.bgView.layer.borderColor = [UIColor telinkBorderColor].CGColor;
    //masksToBounds
    self.bgView.layer.masksToBounds = YES;
    //backgroundColor
    self.backgroundColor = [UIColor clearColor];
}

//根据是否选中更新UI
- (void)setUIWithSelected:(BOOL)isSelected {
    self.bgView.layer.borderColor = isSelected ? [UIColor telinkButtonBlue].CGColor : [UIColor telinkBorderColor].CGColor;
}

- (void)setSelected:(BOOL)selected animated:(BOOL)animated {
    [super setSelected:selected animated:animated];

    // Configure the view for the selected state
}

@end