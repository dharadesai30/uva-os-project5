#!/bin/bash
# from NJU Navy "slider"
# xzl: Not working well. 1. needs root to install "imagemagick" which by default
# not installed. 
# 2. has bugs https://stackoverflow.com/questions/52998331/imagemagick-security-policy-pdf-blocking-conversion
#   which needs root to fix

# we need: 
# 1. auto convert pdf or pptx to MULTIPLE bmps. 
# 2. resize bmps

# viable routes: 
# 1a use pptx to export multiple bmps. 
# 1b. use Adobe's online tool: export PDF to multiple jpgs (no bmps)
# 2. use some online tool to batch convert/resize bmps/jpgs to smaller bmps
#   https://redketchup.io/bulk-image-resizer    
#     (note - color depth: 24bit; resize by filesize: 200KB
# then manual rename 


# below are old commands for ref
convert slides.pdf \
  -sharpen "0x1.0" \
  -type truecolor -resize 400x300\! slides-%3d.bmp

# mkdir -p $NAVY_HOME/fsimg/share/slides/
# rm $NAVY_HOME/fsimg/share/slides/*
# mv *.bmp $NAVY_HOME/fsimg/share/slides/


# resize images...
# convert slides/large/Slide2.BMP -resize 320x240  slides/Slide2.BMP