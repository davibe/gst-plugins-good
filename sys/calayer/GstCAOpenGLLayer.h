#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

@interface GstCAOpenGLLayer : CAOpenGLLayer
{
  void *buffer;
  unsigned int pi_texture;
  int width, height;
  bool needsReinit;
}

- (void) setTextureBuffer: (void *) buf;
- (void) setVideoSize: (int) w: (int) h;
- (void) initializeTexture;
@end
