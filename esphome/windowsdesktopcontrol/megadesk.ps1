$url = "http://[IP OR DNS OF YOUR ESP DEVICE]/button/desk_position_$($args[0])/press"
Invoke-RestMethod -Method 'Post' -Uri $url
