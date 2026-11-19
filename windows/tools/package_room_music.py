"""Prepare original ATRAC3 banks for the first room and belongings screen."""
from pathlib import Path

from audio_prepare import inspect_sgd
from iso_reader import require


TRACKS = {
    "BGM_DAYORI": "43152cc623cae680539e91224177c0f1c41aecd8d058e7242aa222f6356a64d5",
    "asa1": "a97dbbd764ddf807c7f03dd41ff082f7166b89896774a4f2699574c3d0337b1f",
    "asa2": "fbe2ea3fc7d8ad6d97427149f17192ec2ea0049691fad251d8e78946aee4f394",
    "hiru1": "f062c890aa32db0dfff5dd2430907ae35d5a21854cd6d82638099b53693d114b",
    "hiru2": "0410df1144b71d01aa9a46856530a04821b19b302e5df82c26ba42df6b349705",
    "hiru3": "66d806b88c60641d8543069625ed467aaecf9cb271895b6a9b7857b60864bf13",
    "hiru4": "9eff457144c5a30a7c37ada4c2953ee37162e0ecbfd9c93014b8cc6ab66d1c41",
    "hiru5": "be0bcf8312f88da1ef9e2f4f1c0e3d338f56e92d93e54424dbc01a097b7c777a",
    "yuu1": "953230ff26168f6dca42318b15bbf526eacd7fe0c857b606f7b0633570f619ed",
    "yuu2": "5b82e13e8ab267e9bd773c2fbaa4d9131c816e1e8d46453dab8dafc185a11f8b",
    "yoru1": "6ff7bc7216ec1fc41fe9b7d1b31fb9bd9657356ede96834142722013abee1986",
    "yoru2": "53d8f37aa169b20e33899b0429c64b97127a8605f96500c8c22b499f3f578d6f",
    "yoru3": "16aeb215d9d302073d573276274809c5d988a3f896ae890639969880fcf2cee2",
    "yoru4": "263829d3d0be1aa18749a9a7a0c544fa98bcb9156e71b1fa77932f40eba373d3",
    "yoru5": "3ceeacf14b4baf247cd5419e0e28d5f06c777397413df0d1cc19ae771dd9221d",
    "yoru6": "4731f6bb7c7ba54f552b04584924f6dab309bea14ace77fa9c6999d5afb58a62",
}


def room_music(iso):
    files = {}
    for name, digest in TRACKS.items():
        bank = iso.read_file(f"PSP_GAME/USRDIR/data/sound/bgm/{name}.sgd")
        wave, = inspect_sgd(bank.read(0, bank.size))["waves"]
        require(wave["name"] == name and wave["data_sha256"] == digest, f"Unexpected room music {name}")
        files[Path(f"assets/audio/{name}.at3")] = bank.subregion(wave["data_offset"], wave["data_size"]), digest
    return files
