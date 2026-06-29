#version 140

uniform vec3 object_id_color;

out vec4 out_color;

void main()
{
    out_color = vec4(object_id_color, 1.0);
}

