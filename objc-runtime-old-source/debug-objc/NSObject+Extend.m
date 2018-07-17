//
//  NSObject+Extend.m
//  debug-objc
//
//  Created by ZYN on 2018/7/16.
//

#import "NSObject+Extend.h"
#import <objc/runtime.h>

@implementation NSObject (Extend)

+ (void)load {
    NSLog(@"load method");
}

- (NSString *)customName {
    return objc_getAssociatedObject(self, _cmd);
}

- (void)setCustomName:(NSString *)customName {
    objc_setAssociatedObject(self, @selector(customName), customName, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

@end
