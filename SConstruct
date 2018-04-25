env = Environment(CCFLAGS = '-Werror -I/usr/include'
                  , LIBS = ['X11', 'Xdamage', 'Xext', 'Xfixes', 'Xrandr', 'cairo', 'webp'])
conf = Configure(env)
files = ['x-viredero.c', 'ppm.c', 'net.c']
if conf.CheckLib('usb-1.0') :
    env.Append(CCFLAGS=' -DWITH_USB=1')
    files.append('usb.c')

ut = ARGUMENTS.get('usbtest', 0)
if int(ut) :
    files = ['usb-tst.c', 'usb.c']
    env.Program('usb-tst', files)
else :       
    prgm = env.Program('x-viredero', files)
    dst = ARGUMENTS.get('DESTDIR', '') + '/usr/bin'
    env.Install(dst, prgm)
    env.Alias('install', dst)
Export('env')
if 'debian' in COMMAND_LINE_TARGETS:
    SConscript("deb/SConscript")

  
