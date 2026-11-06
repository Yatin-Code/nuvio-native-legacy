.PHONY: test-minipool

test-minipool:
	./tests/minipool_episode_key.sh
	./tests/minipool_hint_requests.sh
	./tests/minipool_invite.sh
	./tests/minipool_pairing.sh
	./tests/minipool_upload_conflict.sh
