
//  main.m
//  debug-objc
//
//  Created by closure on 2/24/16.
//
//

#import <Foundation/Foundation.h>
#import "People.h"
#import "NSObject+Extend.h"
#import <objc/runtime.h>
#import <malloc/malloc.h>

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        
    

            
       People *people = [[People alloc] init];
        people.customName = @"哈哈";
//        people.test = @"666";
            
        class_getInstanceSize([People class]);
            
//        NSLog(@"--- %@", people.customName);
        
//        __weak NSString *test = @"11111";
        
    }
    return 0;
}
