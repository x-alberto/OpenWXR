/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2018 Saso Kiselkov. All rights reserved.
 */

#version 460

#ifdef GL_EXT_gpu_shader4
#extension GL_EXT_gpu_shader4: enable
#endif

layout(location = 10) uniform sampler2D	tex;

layout(location = 11) uniform uint col_ori;
layout(location = 12) uniform uint col_rep;

layout(location = 0) in vec2		tex_coord;

layout(location = 0) out vec4		color_out;

void
main()
{
    uint col = 0xd864a5ff;
    uint rep = 0x78c255ff;

    //uint col = col_ori;
    //uint rep = col_rep;

    //if(rep == 0x000000ff)
        //rep = 0x0000ffff;

//    uint alpha = ((rep>> 24)&0xFF);
//    uint blue = ((rep>> 16)&0xFF);
//    uint green = ((rep>> 8)&0xFF);
//    uint red = (rep & 0xFF);



	vec4 pixel = texture(tex, tex_coord);
	vec2 tex_size = textureSize(tex, 0);
	//vec4 eps = vec4(0.009, 0.009, 0.009, 0.009);
//    vec4 color1 = vec4(((color & 0x000000FF) >> 0)/255.0, ((color & 0x0000FF00) >> 8)/255.0, ((color & 0x00FF0000) >> 16)/255.0, ((color & 0xFF000000)>> 24)/255.0);
//    vec4 replace1 = vec4(((replace & 0x000000FF) >> 0)/255.0, ((replace & 0x0000FF00) >> 8)/255.0, ((replace & 0x00FF0000) >> 16)/255.0, ((replace & 0xFF000000)>> 24)/255.0);



//	vec4 eps = vec4(0.009, 0.009, 0.009, 1.0);
//    vec4 replace1 = vec4(0.0, 0.0, 0.0, 1.0);
//    vec4 color1 = vec4(216.0/255.0, 100.0/255.0, 165.0/255.0, 1.0);
   // vec4 replace1 = vec4(0.0, 0.0, 1.0, 0.5);

    //vec4 replace1 = vec4( red/255.0, green/255.0, blue/255.0, alpha/255.0);

//    if( all( greaterThanEqual(pixel, vec4(color1 - eps)) ) && all( lessThanEqual(pixel, vec4(color1 + eps)) ) )
//             pixel = vec4(replace1);

    vec4 color1 = vec4(((col & 0xFF000000)>> 24)/255.0, ((col & 0x00FF0000) >> 16)/255.0, ((col & 0x0000FF00) >> 8)/255.0, ((col & 0x000000FF) >> 0)/255.0); //
    vec4 replace1 = vec4(((rep & 0xFF000000)>> 24)/255.0, ((rep & 0x00FF0000) >> 16)/255.0, ((rep & 0x0000FF00) >> 8)/255.0, ((rep & 0x000000FF) >> 0)/255.0); //

        if( all( greaterThanEqual(pixel, vec4(color1)) ) )
             pixel = vec4(replace1);



	color_out = pixel;
}
