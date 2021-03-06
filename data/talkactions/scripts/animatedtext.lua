function onSay(cid, words, param)
	if(param == "") then
		doPlayerSendTextMessage(cid, MESSAGE_STATUS_CONSOLE_BLUE, "Command param required.")
		return TRUE
	end

	local t = string.explode(param, ",")
	local tmp = t[1]
	if(t[2]) then
		tmp = t[2]
	end

	t[1] = tonumber(t[1])
	if(t[1] > 0 and t[1] < 255) then
		doSendAnimatedText(getCreaturePosition(cid), tmp, t[1])
	else
		doPlayerSendTextMessage(cid, MESSAGE_STATUS_CONSOLE_BLUE, "Typed color has to be between 0 and 255")
	end
	return TRUE
end
