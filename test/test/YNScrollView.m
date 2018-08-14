//
//  YNScrollView.m
//  test
//
//  Created by ZYN on 2018/8/14.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import "YNScrollView.h"

@interface YNScrollView () <UIGestureRecognizerDelegate>
@end

@implementation YNScrollView



- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer {
    
    
    return YES;
}

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect {
    // Drawing code
}
*/

@end
