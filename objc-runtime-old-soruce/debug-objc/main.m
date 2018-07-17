
//  main.m
//  debug-objc
//
//  Created by closure on 2/24/16.
//
//

#import <Foundation/Foundation.h>
#import "People.h"
#import "NSObject+Extend.h"

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        
        People *people = [[People alloc] init];
        people.customName = @"哈哈";
        
        NSLog(@"--- %@", people.customName);
        
    }
    return 0;
}
