//
//  JH264InMP4.h
//  mp4EncTest
//
//  Created by John Hsu on 2016/2/15.
//  Copyright © 2016年 test. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface JH264InMP4 : NSObject
+(void)convertRawH264File:(NSString *)h264Path toMP4File:(NSString *)mp4Path;
@end
