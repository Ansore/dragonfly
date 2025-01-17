<p align="center">
  <a href="https://dragonflydb.io">
    <img src="https://raw.githubusercontent.com/dragonflydb/dragonfly/main/.github/images/logo-full.svg"
      width="284" border="0" alt="Dragonfly">
  </a>
</p>


# Quick Start

Starting with `docker run` is the simplest way to get up and running with DragonflyDB.

If you do not have docker on your machine, [Install Docker](https://docs.docker.com/get-docker/) before continuing.

## Step 1

```bash
docker run --network=host --ulimit memlock=-1 docker.dragonflydb.io/dragonflydb/dragonfly
```

Dragonfly DB will answer to both `http` and `redis` requests out of the box!

You can use `redis-cli` to connect to `localhost:6379` or open a browser and visit `http://localhost:6379`

## Step 2

Connect with a redis client

```bash
redis-cli
127.0.0.1:6379> set hello world
OK
127.0.0.1:6379> keys *
1) "hello"
127.0.0.1:6379> get hello
"world"
127.0.0.1:6379> 
```

## Step 3

Continue being great and build your app with the power of DragonflyDB!

### More Build Options
- [Docker Compose Deployment](/contrib/docker/)
- [Kubernetes Deployment with Helm Chart](/contrib/charts/dragonfly/)
- [Build From Source](/docs/build-from-source.md)