#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <gst/video/gstvideosink.h>

@interface GstCAOpenGLLayer : CAOpenGLLayer
{
  GstBuffer *current_buffer;
  GstBuffer *previous_buffer;
  unsigned int pi_texture;
  int width, height;
  bool needsReinit;
}

- (void) setTextureBuffer: (GstBuffer *) buf;
- (void) setVideoSize: (int) w: (int) h;
- (void) initializeTexture;
@end
