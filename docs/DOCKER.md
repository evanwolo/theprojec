# Docker Deployment Guide

## Quick Start

### Build the Image
```bash
docker build -t grand-strategy-kernel:latest .
```

### Run Interactive Mode
```bash
docker run -it --rm \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest
```

### Run Batch Simulation
```bash
# Run 1000 ticks, log every 10 steps
echo "run 1000 10" | docker run -i --rm \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest
```

## Docker Compose

### Interactive Mode (Default)
```bash
docker compose up kernel
```
Then interact with the simulation:
```
metrics
step 100
run 1000 10
quit
```

### Batch Mode
```bash
# Run with default parameters (1000 ticks)
docker compose --profile batch up batch

# Run with custom parameters
TICKS=5000 LOG_INTERVAL=50 docker compose --profile batch up batch
```

### Legacy Prototype
```bash
docker compose --profile legacy up legacy
```

## Advanced Usage

### Custom Configuration
```bash
docker run -it --rm \
  -v $(pwd)/data:/app/data \
  -e POPULATION=100000 \
  -e REGIONS=400 \
  grand-strategy-kernel:latest
```

### Run Specific Commands
```bash
# Get metrics only
echo "metrics" | docker run -i --rm grand-strategy-kernel:latest

# Step and export
echo -e "step 500\nstate traits" | docker run -i --rm \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest > data/snapshot.json
```

### Multi-Stage Pipeline
```bash
# Stage 1: Initialize and run 1000 ticks
echo "run 1000 10" | docker run -i \
  --name sim-stage1 \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest

# Stage 2: Continue from checkpoint (if implemented)
echo "step 500" | docker run -i --rm \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest
```

### Performance Tuning
```bash
# Allocate more CPU cores
docker run -it --rm \
  --cpus="4" \
  --memory="2g" \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest

# With CPU affinity
docker run -it --rm \
  --cpuset-cpus="0-3" \
  -v $(pwd)/data:/app/data \
  grand-strategy-kernel:latest
```

## Kubernetes Deployment

### Create ConfigMap
```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: kernel-config
data:
  simulation.txt: |
    run 10000 100
    quit
```

### Deploy Pod
```yaml
apiVersion: v1
kind: Pod
metadata:
  name: kernel-simulation
spec:
  containers:
  - name: kernel
    image: grand-strategy-kernel:latest
    resources:
      requests:
        memory: "1Gi"
        cpu: "2"
      limits:
        memory: "4Gi"
        cpu: "4"
    volumeMounts:
    - name: data
      mountPath: /app/data
    - name: config
      mountPath: /app/config
    command: ["/bin/sh", "-c"]
    args: ["cat /app/config/simulation.txt | /app/KernelSim"]
  volumes:
  - name: data
    persistentVolumeClaim:
      claimName: simulation-data
  - name: config
    configMap:
      name: kernel-config
```

### Job for Batch Processing
```yaml
apiVersion: batch/v1
kind: Job
metadata:
  name: kernel-batch-job
spec:
  template:
    spec:
      containers:
      - name: kernel
        image: grand-strategy-kernel:latest
        command: ["/bin/sh", "-c"]
        args: ["echo 'run 10000 100' | /app/KernelSim"]
        volumeMounts:
        - name: data
          mountPath: /app/data
      restartPolicy: Never
      volumes:
      - name: data
        persistentVolumeClaim:
          claimName: simulation-data
  backoffLimit: 3
```

## Cloud Deployment

### AWS ECS Task Definition
```json
{
  "family": "grand-strategy-kernel",
  "containerDefinitions": [
    {
      "name": "kernel",
      "image": "your-registry/grand-strategy-kernel:latest",
      "memory": 2048,
      "cpu": 1024,
      "essential": true,
      "command": [
        "/bin/sh",
        "-c",
        "echo 'run 10000 100' | /app/KernelSim"
      ],
      "mountPoints": [
        {
          "sourceVolume": "data",
          "containerPath": "/app/data"
        }
      ]
    }
  ],
  "volumes": [
    {
      "name": "data",
      "efsVolumeConfiguration": {
        "fileSystemId": "fs-xxxxx"
      }
    }
  ]
}
```

### Google Cloud Run
```bash
# Build and push
docker build -t gcr.io/PROJECT-ID/kernel:latest .
docker push gcr.io/PROJECT-ID/kernel:latest

# Deploy
gcloud run deploy kernel-simulation \
  --image gcr.io/PROJECT-ID/kernel:latest \
  --platform managed \
  --region us-central1 \
  --memory 2Gi \
  --cpu 2 \
  --timeout 3600
```

## CI/CD Integration

### GitHub Actions
```yaml
name: Build and Push Docker Image

on:
  push:
    branches: [ main ]
    tags: [ 'v*' ]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v3
    
    - name: Build image
      run: docker build -t grand-strategy-kernel:${{ github.sha }} .
    
    - name: Run tests
      run: |
        echo "metrics" | docker run -i grand-strategy-kernel:${{ github.sha }}
    
    - name: Push to registry
      if: github.event_name == 'push' && contains(github.ref, 'refs/tags/')
      run: |
        echo "${{ secrets.DOCKER_PASSWORD }}" | docker login -u "${{ secrets.DOCKER_USERNAME }}" --password-stdin
        docker tag grand-strategy-kernel:${{ github.sha }} username/kernel:latest
        docker push username/kernel:latest
```

## Monitoring

### Export Metrics to Prometheus
```bash
# Run simulation with metrics export
docker run -d \
  --name kernel-monitored \
  -v $(pwd)/data:/app/data \
  -p 8080:8080 \
  grand-strategy-kernel:latest \
  sh -c 'while true; do echo "metrics" | /app/KernelSim > /app/data/metrics.txt; sleep 10; done'
```

### Volume Inspection
```bash
# Check output files
docker run --rm \
  -v $(pwd)/data:/app/data \
  alpine ls -lah /app/data

# View metrics
docker run --rm \
  -v $(pwd)/data:/app/data \
  alpine cat /app/data/metrics.csv
```

## Troubleshooting

### Check Container Logs
```bash
docker logs kernel-sim
docker logs --follow kernel-batch
```

### Interactive Debugging
```bash
docker run -it --rm \
  -v $(pwd)/data:/app/data \
  --entrypoint /bin/bash \
  grand-strategy-kernel:latest

# Inside container
cd /app
./KernelSim
ls -la data/
```

### Resource Monitoring
```bash
# Monitor container resources
docker stats kernel-sim

# Inspect container
docker inspect kernel-sim
```

## Production Best Practices

1. **Use multi-stage builds** (already implemented) to minimize image size
2. **Run as non-root user** (already implemented)
3. **Use health checks** for long-running containers
4. **Mount volumes** for persistent data
5. **Set resource limits** to prevent resource exhaustion
6. **Use compose profiles** for different deployment scenarios
7. **Tag images** with version numbers for reproducibility
8. **Enable logging** to centralized logging system

## Performance Tips

- **CPU**: 2-4 cores recommended for 50k agents
- **Memory**: 1-2GB sufficient for 50k agents, 4GB+ for 100k+
- **Storage**: SSD recommended for metrics.csv writes
- **Network**: Not critical unless using distributed setup

## Example Production Setup

```bash
# Production deployment with monitoring
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d

# Scale batch workers
docker compose --profile batch up --scale batch=4
```

See `README.md` for simulation commands and `DESIGN.md` for architecture details.
