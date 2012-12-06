**基于TWRP2.3汉化,支持中文字符,并兼容恢复cwm的备份包**

已生成部分字库,默认为32号雅黑字体(font\_cn\_32x32.h),可使用BOARD\_USE\_CUSTOM\_RECOVERY\_FONT指定字体

增加扩展字库,可扩展单个字符,扩展字库中不存在的使用内置字库(感谢[syhost](https://github.com/syhost)的代码)

扩展字库可由minuitwrp/font\_dat\_gen.py生成,只要把需要的字符放到UTF8.txt中以utf8编码保存即可
需要注意的是官方的扩展字库会覆盖.h字库里面的英文,造成中英文显示不协调,只需要移除gui/devices/\*/fonts/\*.dat或替换为自行生成的扩展字库即可


已汉化所有分辨率的主题,并添加了清除电量统计的选项(注意:该选项位置放在了条件显示的清除SD-EXT上,若需要清除SD-EXT(非外置SD)的,请自行修改对应分辨率的ui.xml)

-------------------------------------
**Team Win Recovery Project (TWRP)**

The goal of this branch is to rebase TWRP onto AOSP while maintaining as much of the original AOSP code as possible. This goal should allow us to apply updates to the AOSP code going forward with little to no extra work.  With this goal in mind, we will carefully consider any changes needed to the AOSP code before allowing them.  In most cases, instead of changing the AOSP code, we'll create our own functions instead.  The only changes that should be made to AOSP code should be those affecting startup of the recovery and some of the make files.

This branch is currently a work in progress, however, most features have been implemented and it is now ready for testing.

You can find a compiling guide [here](http://rootzwiki.com/topic/23903-how-to-compile-twrp-from-source/ "Guide").

[More information about the project.](http://www.teamw.in/project/twrp2 "More Information")

If you have code changes to submit those should be pushed to our gerrit instance.  A guide can be found [here](http://teamw.in/twrp2-gerrit "Gerrit Guide").
