env = Environment(CCFLAGS = '-Werror', LIBS = ['X11', 'Xdamage', 'Xext'])
conf = Configure(env)
files = ['x-viredero.c', 'bmp.c']
if conf.CheckLib('usb-1.0') :
    env.Append(CCFLAGS=' -DWITH_USB=1')
    files.append('usb.c')
    
env.Program('x-viredero', files)
