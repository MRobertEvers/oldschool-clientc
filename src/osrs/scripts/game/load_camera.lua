local cam = Game.Misc.load_camera()
if cam then
    _G.camera_x = cam[1]
    _G.camera_y = cam[2]
    _G.camera_z = cam[3]
    _G.camera_pitch = cam[4]
    _G.camera_yaw = cam[5]
end
