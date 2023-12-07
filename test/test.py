import requests
from os.path import dirname, realpath

requests.packages.urllib3.disable_warnings()

test_dir = dirname(realpath(__file__))

# http 301
r = requests.get('http://localhost/index.html', allow_redirects=False)
assert(r.status_code == 301 and r.headers['Location'] == 'https://localhost/index.html')

# https 200 OK
r = requests.get('https://localhost/index.html', verify=False)
assert(r.status_code == 200 and open(test_dir + '/../index.html', 'rb').read() == r.content)

# http 200 OK
r = requests.get('http://localhost/index.html', verify=False)
assert(r.status_code == 200 and open(test_dir + '/../index.html', 'rb').read() == r.content)

# http 404
r = requests.get('http://localhost/notfound.html', verify=False)
assert(r.status_code == 404)

# file in directory
r = requests.get('http://localhost/dir/index.html', verify=False)
assert(r.status_code == 200 and open(test_dir + '/../index.html', 'rb').read() == r.content)

# http 206
headers = { 'Range': 'bytes=100-200' }
r = requests.get('http://localhost/index.html', headers=headers, verify=False)
# print(r.headers)
# print(r.content)
assert(r.status_code == 206 and open(test_dir + '/../index.html', 'rb').read()[100:201] == r.content)

# http 206
headers = { 'Range': 'bytes=100-' }
r = requests.get('http://localhost/index.html', headers=headers, verify=False)
# print(r.headers)
assert(r.status_code == 206 and open(test_dir + '/../index.html', 'rb').read()[100:] == r.content)
