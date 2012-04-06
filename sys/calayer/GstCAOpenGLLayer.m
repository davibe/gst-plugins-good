#import <OpenGL/OpenGL.h>
#import "GstCAOpenGLLayer.h"

@implementation GstCAOpenGLLayer

- (id) init 
{
  id ret;
  pi_texture = 0;
  width = 0;
  height = 0;
  current_buffer = NULL;
  previous_buffer = NULL;

  needsReinit = true;
  ret = [super init];
  self.asynchronous = NO;
  return ret;
}

- (void) setTextureBuffer: (GstBuffer *) buf
{
  if (current_buffer != NULL)
    previous_buffer = current_buffer;

  current_buffer = buf;
  gst_buffer_ref (current_buffer);
}

- (void) setVideoSize: (int) w: (int) h {
  width = w; height = h;
}

- (void) initializeTexture 
{
  if (pi_texture) {
    glDeleteTextures (1, (GLuint *)&pi_texture);
  }

  glGenTextures (1, (GLuint *)&pi_texture);

  glEnable (GL_TEXTURE_RECTANGLE_EXT);
  glEnable (GL_UNPACK_CLIENT_STORAGE_APPLE);

  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, width);
  
  glBindTexture (GL_TEXTURE_RECTANGLE_EXT, pi_texture);

  /* Use VRAM texturing */
  glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
       GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_CACHED_APPLE);

  /* Tell the driver not to make a copy of the texture but to use
     our buffer */
  glPixelStorei (GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);

  /* Linear interpolation */
  glTexParameteri (GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
       GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_EXT,
       GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* Initialize the texture with glTexImage2D.
   * Later we use glTexSubImage2D to redefine the storage area */
  glTexImage2D (GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA,
    width, height, 0, 
    GL_YCBCR_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE,
    GST_BUFFER_DATA (current_buffer));
  
  needsReinit = false;
}

-(CGLPixelFormatObj)copyCGLPixelFormatForDisplayMask:(uint32_t)mask
{
  return [super copyCGLPixelFormatForDisplayMask:mask];
}

-(CGLContextObj)copyCGLContextForPixelFormat:(CGLPixelFormatObj)pixelFormat
{
  return [super copyCGLContextForPixelFormat:pixelFormat];
}

-(BOOL)canDrawInCGLContext:(CGLContextObj)glContext 
    pixelFormat:(CGLPixelFormatObj)pixelFormat 
    forLayerTime:(CFTimeInterval)timeInterval 
    displayTime:(const CVTimeStamp *)timeStamp
{
  return YES;
}

- (BOOL) isAsynchronous {
  return NO;
}

-(void)drawInCGLContext:(CGLContextObj)glContext 
    pixelFormat:(CGLPixelFormatObj)pixelFormat 
    forLayerTime:(CFTimeInterval)timeInterval 
    displayTime:(const CVTimeStamp *)timeStamp
{
  GLfloat f_x, f_y;
  GLint params[] = { 1 };
  f_x = 1.0;
  f_y = 1.0;

  /* Skip in case we are asked to draw with no buffer in place.
   * This may happen if the application calls setNeedsDisplay but there is no 
   * buffer ready yet */
  if (current_buffer == NULL)
    return;

  CGLSetCurrentContext(glContext);
  CGLSetParameter (CGLGetCurrentContext (), kCGLCPSwapInterval, params);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (needsReinit)
    [self initializeTexture];


  // reaload texture
  glBindTexture (GL_TEXTURE_RECTANGLE_EXT, pi_texture);
  glPixelStorei (GL_UNPACK_ROW_LENGTH, width);

  glTexSubImage2D (GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0,
       width, height,
       GL_YCBCR_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE,
       GST_BUFFER_DATA (current_buffer));

  // bind
  glBindTexture (GL_TEXTURE_RECTANGLE_EXT, pi_texture);

  glBegin (GL_QUADS);
  /* Top left */
  glTexCoord2f (0.0, 0.0);
  glVertex2f (-f_x, f_y);
  /* Bottom left */
  glTexCoord2f (0.0, (float) height);
  glVertex2f (-f_x, -f_y);
  /* Bottom right */
  glTexCoord2f ((float) width, (float) height);
  glVertex2f (f_x, -f_y);
  /* Top right */
  glTexCoord2f ((float) width, 0.0);
  glVertex2f (f_x, f_y);
  glEnd ();

  [super drawInCGLContext:glContext pixelFormat:pixelFormat 
      forLayerTime:timeInterval displayTime:timeStamp];
  
  if (previous_buffer != NULL) {
    gst_buffer_unref (previous_buffer);
    previous_buffer = NULL;
  }
}

-(void)releaseCGLContext:(CGLContextObj)glContext
{
  needsReinit = true;
  [super releaseCGLContext:glContext];
}

-(void)releaseCGLPixelFormat:(CGLPixelFormatObj)pixelFormat
{
  [super releaseCGLPixelFormat:pixelFormat];
}

@end
